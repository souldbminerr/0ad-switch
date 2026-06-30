/* Copyright (C) 2025 Wildfire Games.
 * This file is part of 0 A.D.
 *
 * 0 A.D. is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * 0 A.D. is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with 0 A.D.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "precompiled.h"

#include "XmppClient.h"

#include "StanzaExtensions.h"
#include "i18n/L10n.h"
#include "lib/external_libraries/gloox.h"
#include "lib/utf8.h"
#include "lobby/IXmppClient.h"
#include "network/NetClient.h"
#include "network/NetServer.h"
#include "network/StunClient.h"
#include "ps/CLogger.h"
#include "ps/CStr.h"
#include "ps/ConfigDB.h"
#include "ps/GUID.h"
#include "ps/Pyrogenesis.h"
#include "scriptinterface/Object.h"
#include "scriptinterface/ScriptConversions.h"
#include "scriptinterface/ScriptInterface.h"
#include "scriptinterface/ScriptRequest.h"
#include "scriptinterface/StructuredClone.h"

#include <iostream>
#include <js/GCAPI.h>
#include <js/PropertyAndElement.h>
#include <js/PropertyDescriptor.h>
#include <js/TracingAPI.h>
#include <memory>
#include <tuple>
#include <unicode/locid.h>
#include <unicode/utypes.h>
#include <utility>

//debug
#if 1
#define DbgXMPP(x)
#else
#define DbgXMPP(x) std::cout << x << std::endl;

static std::string tag_xml(const gloox::IQ& iq)
{
	return iq.tag()->xml();
}
#endif

static std::string tag_name(const gloox::IQ& iq)
{
	return iq.tag()->name();
}

IXmppClient* IXmppClient::create(const ScriptInterface* scriptInterface, const std::string& sUsername, const std::string& sPassword, const std::string& sRoom, const std::string& sNick, const int historyRequestSize,bool regOpt)
{
	return new XmppClient(scriptInterface, sUsername, sPassword, sRoom, sNick, historyRequestSize, regOpt);
}

/**
 * Construct the XMPP client.
 *
 * @param scriptInterface - ScriptInterface to be used for storing GUI messages.
 * Can be left blank for non-visual applications.
 * @param sUsername Username to login with of register.
 * @param sPassword Password to login with or register.
 * @param sRoom MUC room to join.
 * @param sNick Nick to join with.
 * @param historyRequestSize Number of stanzas of room history to request.
 * @param regOpt If we are just registering or not.
 */
XmppClient::XmppClient(const ScriptInterface* scriptInterface, const std::string& sUsername, const std::string& sPassword, const std::string& sRoom, const std::string& sNick, const int historyRequestSize, bool regOpt)
	: m_ScriptInterface(scriptInterface),
	  m_client(nullptr),
	  m_mucRoom(nullptr),
	  m_registration(nullptr),
	  m_regOpt(regOpt),
	  m_username(sUsername),
	  m_password(sPassword),
	  m_server{g_ConfigDB.Get("lobby.server", std::string{})},
	  m_room(sRoom),
	  m_nick(sNick),
	  m_xpartamuppId{g_ConfigDB.Get("lobby.xpartamupp", std::string{}) + "@" + m_server + "/CC"},
	  m_echelonId{g_ConfigDB.Get("lobby.echelon", std::string{}) + "@" + m_server + "/CC"},
	  m_initialLoadComplete(false),
	  m_isConnected(false),
	  m_sessionManager(nullptr),
	  m_certStatus(gloox::CertStatus::CertOk),
	  m_PlayerMapUpdate(false),
	  m_connectionDataJid(),
	  m_connectionDataIqId()
{
	if (m_ScriptInterface)
		JS_AddExtraGCRootsTracer(m_ScriptInterface->GetGeneralJSContext(), XmppClient::Trace, this);

	// Generate a unique, unpredictable resource to allow multiple 0 A.D. instances to connect to the lobby.
	gloox::JID clientJid(sUsername + "@" + m_server + "/0ad-" + ps_generate_guid());
	gloox::JID roomJid(m_room + "@conference." + m_server + "/" + sNick);

	// If we are connecting, use the full jid and a password
	// If we are registering, only use the server name
	if (!regOpt)
		m_client = new gloox::Client(clientJid, sPassword);
	else
		m_client = new gloox::Client(m_server);

	// Optionally join without a TLS certificate, so a local server can be tested  quickly.
	// Security risks from malicious JS mods can be mitigated if this option and also the hostname and login are shielded from JS access.
	m_client->setTls(g_ConfigDB.Get("lobby.tls", true) ? gloox::TLSRequired : gloox::TLSDisabled);

	// Disable use of the SASL PLAIN mechanism, to prevent leaking credentials
	// if the server doesn't list any supported SASL mechanism or the response
	// has been modified to exclude those.
	const int mechs = gloox::SaslMechAll ^ gloox::SaslMechPlain;
	m_client->setSASLMechanisms(mechs);

	m_client->registerConnectionListener(this);
	m_client->setPresence(gloox::Presence::Available, -1);
	m_client->disco()->setVersion("Pyrogenesis", PS_SERIALIZATION_VERSION);
	m_client->disco()->setIdentity("client", "bot");
	m_client->setCompression(false);

	m_client->registerStanzaExtension(new GameListQuery());
	m_client->registerIqHandler(this, EXTGAMELISTQUERY);

	m_client->registerStanzaExtension(new BoardListQuery());
	m_client->registerIqHandler(this, EXTBOARDLISTQUERY);

	m_client->registerStanzaExtension(new ProfileQuery());
	m_client->registerIqHandler(this, EXTPROFILEQUERY);

	m_client->registerStanzaExtension(new LobbyAuth());
	m_client->registerIqHandler(this, EXTLOBBYAUTH);

	m_client->registerStanzaExtension(new ConnectionData());
	m_client->registerIqHandler(this, EXTCONNECTIONDATA);

	m_client->registerMessageHandler(this);

	m_registration = new gloox::Registration(m_client);
	m_registration->registerRegistrationHandler(this);

	// Uncomment to see the raw stanzas
	// m_client->logInstance().registerLogHandler(gloox::LogLevelDebug, gloox::LogAreaAll, this);

	if (!regOpt)
	{
		// Create a Multi User Chat Room
		m_mucRoom = new gloox::MUCRoom(m_client, roomJid, this, 0);
		// Get room history.
		m_mucRoom->setRequestHistory(historyRequestSize, gloox::MUCRoom::HistoryMaxStanzas);
	}

	m_sessionManager = new gloox::Jingle::SessionManager(m_client, this);
	// Register plugins to allow gloox parse them in incoming sessions
	m_sessionManager->registerPlugin(new gloox::Jingle::Content());
	m_sessionManager->registerPlugin(new gloox::Jingle::ICEUDP());
}

/**
 * Destroy the xmpp client
 */
XmppClient::~XmppClient()
{
	this->disconnect();

	DbgXMPP("XmppClient destroyed");
	delete m_registration;
	delete m_mucRoom;
	delete m_sessionManager;

	// Workaround for memory leak in gloox 1.0/1.0.1
	m_client->removePresenceExtension(gloox::ExtCaps);

	delete m_client;

	for (const gloox::Tag* const& t : m_GameList)
		delete t;
	for (const gloox::Tag* const& t : m_BoardList)
		delete t;
	for (const gloox::Tag* const& t : m_Profile)
		delete t;

	if (m_ScriptInterface)
		JS_RemoveExtraGCRootsTracer(m_ScriptInterface->GetGeneralJSContext(), XmppClient::Trace, this);
}

void XmppClient::TraceMember(JSTracer* trc)
{
	for (JS::Heap<JS::Value>& guiMessage : m_GuiMessageQueue)
		JS::TraceEdge(trc, &guiMessage, "m_GuiMessageQueue");

	for (JS::Heap<JS::Value>& guiMessage : m_HistoricGuiMessages)
		JS::TraceEdge(trc, &guiMessage, "m_HistoricGuiMessages");
}

/// Network
void XmppClient::connect()
{
	m_initialLoadComplete = false;
	m_client->connect(false);
}

void XmppClient::disconnect()
{
	m_client->disconnect();
}

bool XmppClient::isConnected()
{
	return m_isConnected;
}

void XmppClient::recv()
{
	m_client->recv(1);
}

/**
 * Log (debug) Handler
 */
void XmppClient::handleLog(gloox::LogLevel level, gloox::LogArea area, const std::string& message)
{
	std::cout << "log: level: " << level << ", area: " << area << ", message: " << message << std::endl;
}

/*****************************************************
 * Connection handlers                               *
 *****************************************************/

/**
 * Handle connection
 */
void XmppClient::onConnect()
{
	if (m_mucRoom)
	{
		m_isConnected = true;
		CreateGUIMessage("system", "connected", std::time(nullptr));
		m_mucRoom->join();
	}

	if (m_regOpt)
		m_registration->fetchRegistrationFields();
}

/**
 * Handle disconnection
 */
void XmppClient::onDisconnect(gloox::ConnectionError error)
{
	// Make sure we properly leave the room so that
	// everything works if we decide to come back later
	if (m_mucRoom)
		m_mucRoom->leave();

	// Clear game, board and player lists.
	for (const gloox::Tag* const& t : m_GameList)
		delete t;
	for (const gloox::Tag* const& t : m_BoardList)
		delete t;
	for (const gloox::Tag* const& t : m_Profile)
		delete t;

	m_BoardList.clear();
	m_GameList.clear();
	m_PlayerMap.clear();
	m_PlayerMapUpdate = true;
	m_Profile.clear();
	m_HistoricGuiMessages.clear();
	m_isConnected = false;
	m_initialLoadComplete = false;

	CreateGUIMessage(
		"system",
		"disconnected",
		std::time(nullptr),
		"reason", error,
		"certificate_status", m_certStatus);
}

/**
 * Handle TLS connection.
 */
bool XmppClient::onTLSConnect(const gloox::CertInfo& info)
{
	DbgXMPP("onTLSConnect");
	DbgXMPP(
		"status: " << info.status <<
		"\nissuer: " << info.issuer <<
		"\npeer: " << info.server <<
		"\nprotocol: " << info.protocol <<
		"\nmac: " << info.mac <<
		"\ncipher: " << info.cipher <<
		"\ncompression: " << info.compression );

	m_certStatus = static_cast<gloox::CertStatus>(info.status);

	// Optionally accept invalid certificates, see require_tls option.
	return info.status == gloox::CertOk || !g_ConfigDB.Get("lobby.verify_certificate", true);
}

/**
 * Handle MUC room errors
 */
void XmppClient::handleMUCError(gloox::MUCRoom*, gloox::StanzaError err)
{
	DbgXMPP("MUC Error " << ": " << StanzaErrorToString(err));
	CreateGUIMessage("system", "error", std::time(nullptr), "text", err);
}

/*****************************************************
 * Requests to server                                *
 *****************************************************/

/**
 * Request the leaderboard data from the server.
 */
void XmppClient::SendIqGetBoardList()
{
	gloox::JID echelonJid(m_echelonId);

	// Send IQ
	BoardListQuery* b = new BoardListQuery();
	b->m_Command = "getleaderboard";
	gloox::IQ iq(gloox::IQ::Get, echelonJid, m_client->getID());
	iq.addExtension(b);
	DbgXMPP("SendIqGetBoardList [" << tag_xml(iq) << "]");
	m_client->send(iq);
}

/**
 * Request the profile data from the server.
 */
void XmppClient::SendIqGetProfile(const std::string& player)
{
	gloox::JID echelonJid(m_echelonId);

	// Send IQ
	ProfileQuery* b = new ProfileQuery();
	b->m_Command = player;
	gloox::IQ iq(gloox::IQ::Get, echelonJid, m_client->getID());
	iq.addExtension(b);
	DbgXMPP("SendIqGetProfile [" << tag_xml(iq) << "]");
	m_client->send(iq);
}

/**
 * Request the Connection data (ip, port...) from the server.
 */
void XmppClient::SendIqGetConnectionData(const std::string& jid, const std::string& password, const std::string& clientSalt, bool localIP)
{
	gloox::JID targetJID(jid);

	ConnectionData* connectionData = new ConnectionData();
	connectionData->m_Password = password;
	connectionData->m_ClientSalt = clientSalt;
	connectionData->m_IsLocalIP = localIP ? "1" : "0";
	gloox::IQ iq(gloox::IQ::Get, targetJID, m_client->getID());
	iq.addExtension(connectionData);
	m_connectionDataJid = iq.from().full();
	m_connectionDataIqId = iq.id();
	DbgXMPP("SendIqGetConnectionData [" << tag_xml(iq) << "]");
	m_client->send(iq);
}

/**
 * Send game report containing numerous game properties to the server.
 *
 * @param data A JS array of game statistics
 */
void XmppClient::SendIqGameReport(const ScriptRequest& rq, JS::HandleValue data)
{
	gloox::JID echelonJid(m_echelonId);

	// Setup some base stanza attributes
	GameReport* game = new GameReport();
	gloox::Tag* report = new gloox::Tag("game");

	// Iterate through all the properties reported and add them to the stanza.
	std::vector<std::string> properties;
	Script::EnumeratePropertyNames(rq, data, true, properties);

	// https://gitea.wildfiregames.com/0ad/0ad/issues/8687
	const std::map<std::string, std::string> mappings{
		{ "civilianUnitsLost", "femaleCitizenUnitsLost" },
		{ "civilianUnitsTrained", "femaleCitizenUnitsTrained" },
		{ "enemyCivilianUnitsKilled", "enemyFemaleCitizenUnitsKilled"}
	};
	for (const std::string& p : properties)
	{
		std::wstring value;
		Script::GetProperty(rq, data, p.c_str(), value);
		if (mappings.contains(p))
			report->addAttribute(mappings.at(p), utf8_from_wstring(value));
		else
			report->addAttribute(p, utf8_from_wstring(value));
	}

	// Add stanza to IQ
	game->m_GameReport.emplace_back(report);

	// Send IQ
	gloox::IQ iq(gloox::IQ::Set, echelonJid, m_client->getID());
	iq.addExtension(game);
	DbgXMPP("SendGameReport [" << tag_xml(iq) << "]");
	m_client->send(iq);
};

/**
 * Send a request to register a game to the server.
 *
 * @param data A JS array of game attributes
 */
void XmppClient::SendIqRegisterGame(const ScriptRequest& rq, JS::HandleValue data)
{
	gloox::JID xpartamuppJid(m_xpartamuppId);

	// Setup some base stanza attributes
	std::unique_ptr<GameListQuery> g = std::make_unique<GameListQuery>();
	g->m_Command = "register";
	gloox::Tag* game = new gloox::Tag("game");

	// Iterate through all the properties reported and add them to the stanza.
	std::vector<std::string> properties;
	Script::EnumeratePropertyNames(rq, data, true, properties);
	for (const std::string& p : properties)
	{
		std::string value;
		if (!Script::GetProperty(rq, data, p.c_str(), value))
		{
			LOGERROR("Could not parse attribute '%s' as string.", p);
			return;
		}
		game->addAttribute(p, value);
	}

	// Overwrite some attributes to make it slightly less trivial to do bad things,
	// and explicit some invariants.

	// The JID must point to ourself.
	game->addAttribute("hostJID", GetJID());

	// Push the stanza onto the IQ
	g->m_GameList.emplace_back(game);

	// Send IQ
	gloox::IQ iq(gloox::IQ::Set, xpartamuppJid, m_client->getID());
	iq.addExtension(g.release());
	DbgXMPP("SendIqRegisterGame [" << tag_xml(iq) << "]");
	m_client->send(iq);
}

/**
 * Send a request to unregister a game to the server.
 */
void XmppClient::SendIqUnregisterGame()
{
	gloox::JID xpartamuppJid(m_xpartamuppId);

	// Send IQ
	GameListQuery* g = new GameListQuery();
	g->m_Command = "unregister";
	g->m_GameList.emplace_back(new gloox::Tag("game"));

	gloox::IQ iq(gloox::IQ::Set, xpartamuppJid, m_client->getID());
	iq.addExtension(g);
	DbgXMPP("SendIqUnregisterGame [" << tag_xml(iq) << "]");
	m_client->send(iq);
}

/**
 * Send a request to change the state of a registered game on the server.
 *
 * A game can either be in the 'running' or 'waiting' state - the server
 * decides which - but we need to update the current players that are
 * in-game so the server can make the calculation.
 */
void XmppClient::SendIqChangeStateGame(const std::string& nbp, const std::string& players)
{
	gloox::JID xpartamuppJid(m_xpartamuppId);

	// Send IQ
	GameListQuery* g = new GameListQuery();
	g->m_Command = "changestate";
	gloox::Tag* game = new gloox::Tag("game");
	game->addAttribute("nbp", nbp);
	game->addAttribute("players", players);
	g->m_GameList.emplace_back(game);

	gloox::IQ iq(gloox::IQ::Set, xpartamuppJid, m_client->getID());
	iq.addExtension(g);
	DbgXMPP("SendIqChangeStateGame [" << tag_xml(iq) << "]");
	m_client->send(iq);
}

/*****************************************************
 * iq to clients                                     *
 *****************************************************/

/**
 * Send lobby authentication token.
 */
void XmppClient::SendIqLobbyAuth(const std::string& to, const std::string& token)
{
	LobbyAuth* auth = new LobbyAuth();
	auth->m_Token = token;

	gloox::JID clientJid(to);
	gloox::IQ iq(gloox::IQ::Set, clientJid, m_client->getID());
	iq.addExtension(auth);
	DbgXMPP("SendIqLobbyAuth [" << tag_xml(iq) << "]");
	m_client->send(iq);
}

/*****************************************************
 * Account registration                              *
 *****************************************************/

void XmppClient::handleRegistrationFields(const gloox::JID&, int fields, std::string)
{
	gloox::RegistrationFields vals;
	vals.username = m_username;
	vals.password = m_password;
	m_registration->createAccount(fields, vals);
}

#if GLOOXVERSION >= 0x010100
void XmppClient::handleRegistrationResult(const gloox::JID&, gloox::RegistrationResult result, const gloox::Error* /*error*/)
#else
void XmppClient::handleRegistrationResult(const gloox::JID&, gloox::RegistrationResult result)
#endif
{
	if (result == gloox::RegistrationSuccess)
		CreateGUIMessage("system", "registered", std::time(nullptr));
	else
		CreateGUIMessage("system", "error", std::time(nullptr), "text", result);
}

void XmppClient::handleAlreadyRegistered(const gloox::JID&)
{
	DbgXMPP("the account already exists");
}

void XmppClient::handleDataForm(const gloox::JID&, const gloox::DataForm&)
{
	DbgXMPP("dataForm received");
}

void XmppClient::handleOOB(const gloox::JID&, const gloox::OOB&)
{
	DbgXMPP("OOB registration requested");
}

/*****************************************************
 * Requests from GUI                                 *
 *****************************************************/

/**
 * Handle requests from the GUI for the list of players.
 *
 * @return A JS array containing all known players and their presences
 */
JS::Value XmppClient::GUIGetPlayerList(const ScriptRequest& rq)
{
	JS::RootedValue ret(rq.cx);
	Script::CreateArray(rq, &ret);
	int j = 0;

	for (const std::pair<const std::string, SPlayer>& p : m_PlayerMap)
	{
		JS::RootedValue player(rq.cx);

		Script::CreateObject(
			rq,
			&player,
			"name", p.first,
			"presence", p.second.m_Presence,
			"rating", p.second.m_Rating,
			"role", p.second.m_Role);

		Script::SetPropertyInt(rq, ret, j++, player);
	}
	return ret;
}

/**
 * Handle requests from the GUI for the list of all active games.
 *
 * @return A JS array containing all known games
 */
JS::Value XmppClient::GUIGetGameList(const ScriptRequest& rq)
{
	JS::RootedValue ret(rq.cx);
	Script::CreateArray(rq, &ret);
	int j = 0;

	const char* stats[] = { "name", "hostUsername", "hostJID", "state", "hasPassword",
		"nbp", "maxnbp", "players", "mapName", "niceMapName", "mapSize", "mapType",
		"victoryConditions", "startTime", "mods" };

	for(const gloox::Tag* const& t : m_GameList)
	{
		JS::RootedValue game(rq.cx);
		Script::CreateObject(rq, &game);

		for (size_t i = 0; i < ARRAY_SIZE(stats); ++i)
			Script::SetProperty(rq, game, stats[i], t->findAttribute(stats[i]));

		Script::SetPropertyInt(rq, ret, j++, game);
	}
	return ret;
}

/**
 * Handle requests from the GUI for leaderboard data.
 *
 * @return A JS array containing all known leaderboard data
 */
JS::Value XmppClient::GUIGetBoardList(const ScriptRequest& rq)
{
	JS::RootedValue ret(rq.cx);
	Script::CreateArray(rq, &ret);
	int j = 0;

	const char* attributes[] = { "name", "rank", "rating" };

	for(const gloox::Tag* const& t : m_BoardList)
	{
		JS::RootedValue board(rq.cx);
		Script::CreateObject(rq, &board);

		for (size_t i = 0; i < ARRAY_SIZE(attributes); ++i)
			Script::SetProperty(rq, board, attributes[i], t->findAttribute(attributes[i]));

		Script::SetPropertyInt(rq, ret, j++, board);
	}
	return ret;
}

/**
 * Handle requests from the GUI for profile data.
 *
 * @return A JS array containing the specific user's profile data
 */
JS::Value XmppClient::GUIGetProfile(const ScriptRequest& rq)
{
	JS::RootedValue ret(rq.cx);
	Script::CreateArray(rq, &ret);
	int j = 0;

	const char* stats[] = { "player", "rating", "totalGamesPlayed", "highestRating", "wins", "losses", "rank" };

	for (const gloox::Tag* const& t : m_Profile)
	{
		JS::RootedValue profile(rq.cx);
		Script::CreateObject(rq, &profile);

		for (size_t i = 0; i < ARRAY_SIZE(stats); ++i)
			Script::SetProperty(rq, profile, stats[i], t->findAttribute(stats[i]));

		Script::SetPropertyInt(rq, ret, j++, profile);
	}
	return ret;
}

/*****************************************************
 * Message interfaces                                *
 *****************************************************/

void SetGUIMessageProperty(const ScriptRequest&, JS::HandleObject /*messageObj*/)
{
}

template<typename T, typename... Args>
void SetGUIMessageProperty(const ScriptRequest& rq, JS::HandleObject messageObj, const std::string& propertyName, const T& propertyValue, Args const&... args)
{
	JS::RootedValue scriptPropertyValue(rq.cx);
	Script::ToJSVal(rq, &scriptPropertyValue, propertyValue);
	JS_DefineProperty(rq.cx, messageObj, propertyName.c_str(), scriptPropertyValue, JSPROP_ENUMERATE);
	SetGUIMessageProperty(rq, messageObj, args...);
}

template<typename... Args>
void XmppClient::CreateGUIMessage(
	const std::string& type,
	const std::string& level,
	const std::time_t time,
	Args const&... args)
{
	if (!m_ScriptInterface)
		return;
	ScriptRequest rq(m_ScriptInterface);
	JS::RootedValue message(rq.cx);
	Script::CreateObject(
		rq,
		&message,
		"type", type,
		"level", level,
		"historic", false,
		"time", static_cast<double>(time));

	JS::RootedObject messageObj(rq.cx, message.toObjectOrNull());
	SetGUIMessageProperty(rq, messageObj, args...);
	Script::DeepFreezeObject(rq, message);
	m_GuiMessageQueue.push_back(JS::Heap<JS::Value>(message));
}

bool XmppClient::GuiPollHasPlayerListUpdate()
{
	// The initial playerlist will be received in multiple messages
	// Only inform the GUI after all of these playerlist fragments were received.
	if (!m_initialLoadComplete)
		return false;

	bool hasUpdate = m_PlayerMapUpdate;
	m_PlayerMapUpdate = false;
	return hasUpdate;
}

JS::Value XmppClient::GuiPollNewMessages(const ScriptInterface& guiInterface)
{
	if ((m_isConnected && !m_initialLoadComplete) || m_GuiMessageQueue.empty())
		return JS::UndefinedValue();

	ScriptRequest rq(m_ScriptInterface);

	// Optimize for batch message processing that is more
	// performance demanding than processing a lone message.
	JS::RootedValue messages(rq.cx);
	Script::CreateArray(rq, &messages);

	int j = 0;

	for (const JS::Heap<JS::Value>& message : m_GuiMessageQueue)
	{
		Script::SetPropertyInt(rq, messages, j++, message);

		// Store historic chat messages.
		// Only store relevant messages to minimize memory footprint.
		JS::RootedValue rootedMessage(rq.cx, message);
		std::string type;
		Script::GetProperty(rq, rootedMessage, "type", type);
		if (type != "chat")
			continue;

		std::string level;
		Script::GetProperty(rq, rootedMessage, "level", level);
		if (level != "room-message" && level != "private-message")
			continue;

		JS::RootedValue historicMessage(rq.cx, Script::DeepCopy(rq, rootedMessage));
		if (true)
		{
			Script::SetProperty(rq, historicMessage, "historic", true);
			Script::DeepFreezeObject(rq, historicMessage);
			m_HistoricGuiMessages.push_back(JS::Heap<JS::Value>(historicMessage));
		}
		else
			LOGERROR("Could not clone historic lobby GUI message!");
	}
	m_GuiMessageQueue.clear();

	// Copy the messages over to the caller script interface.
	return Script::CloneValueFromOtherCompartment(guiInterface, *m_ScriptInterface, messages);
}

JS::Value XmppClient::GuiPollHistoricMessages(const ScriptInterface& guiInterface)
{
	if (m_HistoricGuiMessages.empty())
		return JS::UndefinedValue();

	ScriptRequest rq(m_ScriptInterface);

	JS::RootedValue messages(rq.cx);
	Script::CreateArray(rq, &messages);

	int j = 0;
	for (const JS::Heap<JS::Value>& message : m_HistoricGuiMessages)
		Script::SetPropertyInt(rq, messages, j++, message);

	// Copy the messages over to the caller script interface.
	return Script::CloneValueFromOtherCompartment(guiInterface, *m_ScriptInterface, messages);
}

/**
 * Send a standard MUC textual message.
 */
void XmppClient::SendMUCMessage(const std::string& message)
{
	m_mucRoom->send(message);
}

/**
 * Handle a room message.
 */
void XmppClient::handleMUCMessage(gloox::MUCRoom*, const gloox::Message& msg, bool priv)
{
	DbgXMPP(msg.from().resource() << " said " << msg.body());

	CreateGUIMessage(
		"chat",
		priv ? "private-message" : "room-message",
		ComputeTimestamp(msg),
		"from", msg.from().resource(),
		"text", msg.body());
}

/**
 * Handle a private message.
 */
void XmppClient::handleMessage(const gloox::Message& msg, gloox::MessageSession*)
{
	DbgXMPP("type " << msg.subtype() << ", subject " << msg.subject()
	  << ", message " << msg.body() << ", thread id " << msg.thread());

	CreateGUIMessage(
		"chat",
		msg.subtype() == gloox::Message::MessageType::Headline ? "headline" : "private-message",
		ComputeTimestamp(msg),
		"from", msg.from().resource(),
		"subject", msg.subject(),
		"text", msg.body());
}

/**
 * Handle portions of messages containing custom stanza extensions.
 */
bool XmppClient::handleIq(const gloox::IQ& iq)
{
	DbgXMPP("handleIq [" << tag_xml(iq) << "]");

	if (iq.subtype() == gloox::IQ::Result)
	{
		const GameListQuery* gq = iq.findExtension<GameListQuery>(EXTGAMELISTQUERY);
		const BoardListQuery* bq = iq.findExtension<BoardListQuery>(EXTBOARDLISTQUERY);
		const ProfileQuery* pq = iq.findExtension<ProfileQuery>(EXTPROFILEQUERY);
		const ConnectionData* cd = iq.findExtension<ConnectionData>(EXTCONNECTIONDATA);
		if (cd)
		{
			if (g_NetServer || !g_NetClient)
				return true;

			if (!m_connectionDataJid.empty() && m_connectionDataJid.compare(iq.from().full()) != 0) {
				LOGMESSAGE("XmppClient: Received connection data from invalid host: %s", iq.from().username());
				return true;
			}

			if (!m_connectionDataIqId.empty() && m_connectionDataIqId.compare(iq.id()) != 0) {
				LOGMESSAGE("XmppClient: Received connection data with invalid id");
				return true;
			}

			if (!cd->m_Error.empty())
			{
				g_NetClient->HandleGetServerDataFailed(cd->m_Error.c_str());
				return true;
			}

			g_NetClient->SetupServerData(cd->m_Ip, stoi(cd->m_Port));
			g_NetClient->TryToConnectWithSTUN(iq.from().full(), !cd->m_IsLocalIP.empty());
		}
		if (gq)
		{
			if (iq.from().full() != m_xpartamuppId)
			{
				LOGWARNING("XmppClient: Received game list response from unexpected sender: %s", iq.from().full());
				return true;
			}

			for (const gloox::Tag* const& t : m_GameList)
				delete t;
			m_GameList.clear();

			for (const gloox::Tag* const& t : gq->m_GameList)
				m_GameList.emplace_back(t->clone());

			CreateGUIMessage("game", "gamelist", std::time(nullptr));
		}
		if (bq)
		{
			if (iq.from().full() != m_echelonId)
			{
				LOGWARNING("XmppClient: Received board list response from unexpected sender: %s", iq.from().full());
				return true;
			}

			if (bq->m_Command == "boardlist")
			{
				for (const gloox::Tag* const& t : m_BoardList)
					delete t;
				m_BoardList.clear();

				for (const gloox::Tag* const& t : bq->m_StanzaBoardList)
					m_BoardList.emplace_back(t->clone());

				CreateGUIMessage("game", "leaderboard", std::time(nullptr));
			}
			else if (bq->m_Command == "ratinglist")
			{
				for (const gloox::Tag* const& t : bq->m_StanzaBoardList)
				{
					const PlayerMap::iterator it = m_PlayerMap.find(t->findAttribute("name"));
					if (it != m_PlayerMap.end())
					{
						it->second.m_Rating = t->findAttribute("rating");
						m_PlayerMapUpdate = true;
					}
				}
				CreateGUIMessage("game", "ratinglist", std::time(nullptr));
			}
		}
		if (pq)
		{
			if (iq.from().full() != m_echelonId)
			{
				LOGWARNING("XmppClient: Received profile response from unexpected sender: %s", iq.from().full());
				return true;
			}

			for (const gloox::Tag* const& t : m_Profile)
				delete t;
			m_Profile.clear();

			for (const gloox::Tag* const& t : pq->m_StanzaProfile)
				m_Profile.emplace_back(t->clone());

			CreateGUIMessage("game", "profile", std::time(nullptr));
		}
	}
	else if (iq.subtype() == gloox::IQ::Set)
	{
		const LobbyAuth* lobbyAuth = iq.findExtension<LobbyAuth>(EXTLOBBYAUTH);
		if (lobbyAuth)
		{
			LOGMESSAGE("XmppClient: Received lobby auth: %s from %s", lobbyAuth->m_Token, iq.from().username());

			gloox::IQ response(gloox::IQ::Result, iq.from(), iq.id());
			m_client->send(response);

			if (g_NetServer)
				g_NetServer->OnLobbyAuth(iq.from().username(), lobbyAuth->m_Token);
			else
				LOGMESSAGE("Received lobby authentication request, but not hosting currently!");
		}
	}
	else if (iq.subtype() == gloox::IQ::Get)
	{
		const ConnectionData* cd = iq.findExtension<ConnectionData>(EXTCONNECTIONDATA);
		if (cd)
		{
			LOGMESSAGE("XmppClient: Received request for connection data from %s", iq.from().username());
			if (!g_NetServer)
			{
				gloox::IQ response(gloox::IQ::Result, iq.from(), iq.id());
				ConnectionData* connectionData = new ConnectionData();
				connectionData->m_Error = "not_server";

				response.addExtension(connectionData);

				m_client->send(response);
				return true;
			}
			if (g_NetServer->IsBanned(iq.from().username()))
			{
				gloox::IQ response(gloox::IQ::Result, iq.from(), iq.id());
				ConnectionData* connectionData = new ConnectionData();
				connectionData->m_Error = "banned";

				response.addExtension(connectionData);

				m_client->send(response);
				return true;
			}
			if (!g_NetServer->CheckPasswordAndIncrement(iq.from().username(), cd->m_Password, cd->m_ClientSalt))
			{
				gloox::IQ response(gloox::IQ::Result, iq.from(), iq.id());
				ConnectionData* connectionData = new ConnectionData();
				connectionData->m_Error = "invalid_password";

				response.addExtension(connectionData);

				m_client->send(response);
				return true;
			}

			gloox::IQ response(gloox::IQ::Result, iq.from(), iq.id());
			ConnectionData* connectionData = new ConnectionData();

			if (cd->m_IsLocalIP == "0")
			{
				connectionData->m_Ip = g_NetServer->GetPublicIp();
				connectionData->m_Port = std::to_string(g_NetServer->GetPublicPort());
				connectionData->m_IsLocalIP = "";
			}
			else
			{
				CStr ip;
				if (StunClient::FindLocalIP(ip))
				{
					connectionData->m_Ip = ip;
					connectionData->m_Port = std::to_string(g_NetServer->GetLocalPort());
					connectionData->m_IsLocalIP = "true";
				}
				else
					connectionData->m_Error = "local_ip_failed";
			}

			response.addExtension(connectionData);

			m_client->send(response);
		}

	}
	else if (iq.subtype() == gloox::IQ::Error)
		CreateGUIMessage("system", "error", std::time(nullptr), "text", iq.error()->error());
	else
	{
		CreateGUIMessage("system", "error", std::time(nullptr), "text", wstring_from_utf8(g_L10n.Translate("unknown subtype (see logs)")));
		LOGMESSAGE("unknown subtype '%s'", tag_name(iq).c_str());
	}

	return true;
}

/**
 * Update local data when a user changes presence.
 */
void XmppClient::handleMUCParticipantPresence(gloox::MUCRoom*, const gloox::MUCRoomParticipant participant,
	const gloox::Presence& presence)
{
	const std::string& nick = participant.nick->resource();

	if (presence.presence() == gloox::Presence::Unavailable)
	{
		if (!participant.newNick.empty() && (participant.flags & (gloox::UserNickChanged | gloox::UserSelf)))
		{
			// we have a nick change
			if (m_PlayerMap.find(participant.newNick) == m_PlayerMap.end())
				m_PlayerMap.emplace(
					std::piecewise_construct,
					std::forward_as_tuple(participant.newNick),
					std::forward_as_tuple(presence.presence(), participant.role, std::move(m_PlayerMap.at(nick).m_Rating)));
			else
				LOGERROR("Nickname changed to an existing nick!");

			DbgXMPP(nick << " is now known as " << participant.newNick);
			CreateGUIMessage(
				"chat",
				"nick",
				std::time(nullptr),
				"oldnick", nick,
				"newnick", participant.newNick);
		}
		else if (participant.flags & gloox::UserKicked)
		{
			DbgXMPP(nick << " was kicked. Reason: " << participant.reason);
			CreateGUIMessage(
				"chat",
				"kicked",
				std::time(nullptr),
				"nick", nick,
				"reason", participant.reason);
		}
		else if (participant.flags & gloox::UserBanned)
		{
			DbgXMPP(nick << " was banned. Reason: " << participant.reason);
			CreateGUIMessage(
				"chat",
				"banned",
				std::time(nullptr),
				"nick", nick,
				"reason", participant.reason);
		}
		else
		{
			DbgXMPP(nick << " left the room (flags " << participant.flags << ")");
			CreateGUIMessage(
				"chat",
				"leave",
				std::time(nullptr),
				"nick", nick);
		}
		m_PlayerMap.erase(nick);
	}
	else
	{
		const PlayerMap::iterator it = m_PlayerMap.find(nick);

		/* During the initialization process, we receive join messages for everyone
		 * currently in the room. We don't want to display these, so we filter them
		 * out. We will always be the last to join during initialization.
		 */
		if (!m_initialLoadComplete)
		{
			if (m_mucRoom->nick() == nick)
				m_initialLoadComplete = true;
		}
		else if (it == m_PlayerMap.end())
		{
			CreateGUIMessage(
				"chat",
				"join",
				std::time(nullptr),
				"nick", nick);
		}
		else if (it->second.m_Role != participant.role)
		{
			CreateGUIMessage(
				"chat",
				"role",
				std::time(nullptr),
				"nick", nick,
				"oldrole", it->second.m_Role,
				"newrole", participant.role);
		}
		else
		{
			// Don't create a GUI message for regular presence changes, because
			// several hundreds of them accumulate during a match, impacting performance terribly and
			// the only way they are used is to determine whether to update the playerlist.
		}

		DbgXMPP(
			nick << " is in the room, "
			"presence: " << GetPresenceString(presence.presence()) << ", "
			"role: "<< GetRoleString(participant.role));

		if (it == m_PlayerMap.end())
		{
			m_PlayerMap.emplace(
				std::piecewise_construct,
				std::forward_as_tuple(nick),
				std::forward_as_tuple(presence.presence(), participant.role, std::string()));
		}
		else
		{
			it->second.m_Presence = presence.presence();
			it->second.m_Role = participant.role;
		}
	}

	m_PlayerMapUpdate = true;
}

/**
 * Update local cache when subject changes.
 */
void XmppClient::handleMUCSubject(gloox::MUCRoom*, const std::string& nick, const std::string& subject)
{
	m_Subject = wstring_from_utf8(subject);

	CreateGUIMessage(
		"chat",
		"subject",
		std::time(nullptr),
		"nick", nick,
		"subject", m_Subject);
}

/**
 * Get current subject.
 */
const std::wstring& XmppClient::GetSubject()
{
	return m_Subject;
}

/**
 * Request MUC nick change, real change via mucRoomHandler.
 *
 * @param nick Desired MUC nickname
 */
void XmppClient::SetNick(const std::string& nick)
{
	m_mucRoom->setNick(nick);
}

/**
 * Get current MUC nickname.
 */
std::string XmppClient::GetNick() const
{
	return m_mucRoom->nick();
}

std::string XmppClient::GetJID() const
{
	return m_client->jid().full();
}


/**
 * Get the XMPP username.
 *
 * @return current XMPP username
 */
std::string XmppClient::GetUsername() const
{
	return m_username;
}

/**
 * Change password for authenticated user.
 *
 * @param newPassword New password
 */
void XmppClient::ChangePassword(const std::string& newPassword)
{
	m_registration->changePassword(m_client->jid().username(), newPassword);
}

/**
 * Kick a player from the current room.
 *
 * @param nick Nickname to be kicked
 * @param reason Reason the player was kicked
 */
void XmppClient::kick(const std::string& nick, const std::string& reason)
{
	m_mucRoom->kick(nick, reason);
}

/**
 * Ban a player from the current room.
 *
 * @param nick Nickname to be banned
 * @param reason Reason the player was banned
 */
void XmppClient::ban(const std::string& nick, const std::string& reason)
{
	m_mucRoom->ban(nick, reason);
}

/**
 * Change the xmpp presence of the client.
 *
 * @param presence A string containing the desired presence
 */
void XmppClient::SetPresence(const std::string& presence)
{
#define IF(x,y) if (presence == x) m_mucRoom->setPresence(gloox::Presence::y)
	IF("available", Available);
	else IF("chat", Chat);
	else IF("away", Away);
	else IF("playing", DND);
	else IF("offline", Unavailable);
	// The others are not to be set
#undef IF
	else LOGERROR("Unknown presence '%s'", presence.c_str());
}

/**
 * Get the current xmpp presence of the given nick.
 */
const char* XmppClient::GetPresence(const std::string& nick)
{
	const PlayerMap::iterator it = m_PlayerMap.find(nick);

	if (it == m_PlayerMap.end())
		return "offline";

	return GetPresenceString(it->second.m_Presence);
}

/**
 * Get the current xmpp role of the given nick.
 */
const char* XmppClient::GetRole(const std::string& nick)
{
	const PlayerMap::iterator it = m_PlayerMap.find(nick);

	if (it == m_PlayerMap.end())
		return "";

	return GetRoleString(it->second.m_Role);
}

/**
 * Get the most recent received rating of the given nick.
 * Notice that this doesn't request a rating profile if it hasn't been received yet.
 */
std::wstring XmppClient::GetRating(const std::string& nick)
{
	const PlayerMap::iterator it = m_PlayerMap.find(nick);

	if (it == m_PlayerMap.end())
		return std::wstring();

	return wstring_from_utf8(it->second.m_Rating);
}

/*****************************************************
 * Utilities                                         *
 *****************************************************/

/**
 * Parse and return the timestamp of a historic chat message and return the current time for new chat messages.
 * Historic chat messages are implement as DelayedDelivers as specified in XEP-0203.
 * Hence, their timestamp MUST be in UTC and conform to the DateTime format XEP-0082.
 *
 * @returns Seconds since the epoch.
 */
std::time_t XmppClient::ComputeTimestamp(const gloox::Message& msg)
{
	// Only historic messages contain a timestamp!
	if (!msg.when())
		return std::time(nullptr);

	// The locale is irrelevant, because the XMPP date format doesn't contain written month names
	for (const std::string& format : std::vector<std::string>{ "Y-M-d'T'H:m:sZ", "Y-M-d'T'H:m:s.SZ" })
	{
		UDate dateTime = g_L10n.ParseDateTime(msg.when()->stamp(), format, icu::Locale::getUS());
		if (dateTime)
			return dateTime / 1000.0;
	}

	return std::time(nullptr);
}

/**
 * Convert a gloox presence type to an untranslated string literal to be used as an identifier by the scripts.
 */
const char* XmppClient::GetPresenceString(const gloox::Presence::PresenceType presenceType)
{
	switch (presenceType)
	{
#define CASE(X,Y) case gloox::Presence::X: return Y
	CASE(Available, "available");
	CASE(Chat, "chat");
	CASE(Away, "away");
	CASE(DND, "playing");
	CASE(XA, "away");
	CASE(Unavailable, "offline");
	CASE(Probe, "probe");
	CASE(Error, "error");
	CASE(Invalid, "invalid");
	default:
		LOGERROR("Unknown presence type '%d'", static_cast<int>(presenceType));
		return "";
#undef CASE
	}
}

/**
 * Convert a gloox role type to an untranslated string literal to be used as an identifier by the scripts.
 */
const char* XmppClient::GetRoleString(const gloox::MUCRoomRole role)
{
	switch (role)
	{
#define CASE(X, Y) case gloox::X: return Y
	CASE(RoleNone, "none");
	CASE(RoleVisitor, "visitor");
	CASE(RoleParticipant, "participant");
	CASE(RoleModerator, "moderator");
	CASE(RoleInvalid, "invalid");
	default:
		LOGERROR("Unknown role type '%d'", static_cast<int>(role));
		return "";
#undef CASE
	}
}

/**
 * Translates a gloox certificate error codes, i.e. gloox certificate statuses except CertOk.
 * Keep in sync with specifications.
 */
std::string XmppClient::CertificateErrorToString(gloox::CertStatus status)
{
	std::map<gloox::CertStatus, std::string> certificateErrorStrings = {
		{ gloox::CertInvalid, g_L10n.Translate("The certificate is not trusted.") },
		{ gloox::CertSignerUnknown, g_L10n.Translate("The certificate hasn't got a known issuer.") },
		{ gloox::CertRevoked, g_L10n.Translate("The certificate has been revoked.") },
		{ gloox::CertExpired, g_L10n.Translate("The certificate has expired.") },
		{ gloox::CertNotActive, g_L10n.Translate("The certificate is not yet active.") },
		{ gloox::CertWrongPeer, g_L10n.Translate("The certificate has not been issued for the peer connected to.") },
		{ gloox::CertSignerNotCa, g_L10n.Translate("The certificate signer is not a certificate authority.") }
	};

	std::string result;

	for (std::map<gloox::CertStatus, std::string>::iterator it = certificateErrorStrings.begin(); it != certificateErrorStrings.end(); ++it)
		if (status & it->first)
			result += "\n" + it->second;

	return result;
}

/**
 * Convert a gloox stanza error type to string.
 * Keep in sync with Gloox documentation
 *
 * @param err Error to be converted
 * @return Converted error string
 */
std::string XmppClient::StanzaErrorToString(gloox::StanzaError err)
{
#define CASE(X, Y) case gloox::X: return Y
#define DEBUG_CASE(X, Y) case gloox::X: return g_L10n.Translate("Error") + " (" + Y + ")"
	switch (err)
	{
	CASE(StanzaErrorUndefined, g_L10n.Translate("No error"));
	DEBUG_CASE(StanzaErrorBadRequest, "Server received malformed XML");
	CASE(StanzaErrorConflict, g_L10n.Translate("Player already logged in"));
	DEBUG_CASE(StanzaErrorFeatureNotImplemented, "Server does not implement requested feature");
	CASE(StanzaErrorForbidden, g_L10n.Translate("Forbidden"));
	DEBUG_CASE(StanzaErrorGone, "Unable to find message receipiant");
	CASE(StanzaErrorInternalServerError, g_L10n.Translate("Internal server error"));
	DEBUG_CASE(StanzaErrorItemNotFound, "Message receipiant does not exist");
	DEBUG_CASE(StanzaErrorJidMalformed, "JID (XMPP address) malformed");
	DEBUG_CASE(StanzaErrorNotAcceptable, "Receipiant refused message. Possible policy issue");
	CASE(StanzaErrorNotAllowed, g_L10n.Translate("Not allowed"));
	CASE(StanzaErrorNotAuthorized, g_L10n.Translate("Not authorized"));
	DEBUG_CASE(StanzaErrorNotModified, "Requested item has not changed since last request");
	DEBUG_CASE(StanzaErrorPaymentRequired, "This server requires payment");
	CASE(StanzaErrorRecipientUnavailable, g_L10n.Translate("Recipient temporarily unavailable"));
	DEBUG_CASE(StanzaErrorRedirect, "Request redirected");
	CASE(StanzaErrorRegistrationRequired, g_L10n.Translate("Registration required"));
	DEBUG_CASE(StanzaErrorRemoteServerNotFound, "Remote server not found");
	DEBUG_CASE(StanzaErrorRemoteServerTimeout, "Remote server timed out");
	DEBUG_CASE(StanzaErrorResourceConstraint, "The recipient is unable to process the message due to resource constraints");
	CASE(StanzaErrorServiceUnavailable, g_L10n.Translate("Service unavailable"));
	DEBUG_CASE(StanzaErrorSubscribtionRequired, "Service requires subscription");
	DEBUG_CASE(StanzaErrorUnexpectedRequest, "Attempt to send from invalid stanza address");
	DEBUG_CASE(StanzaErrorUnknownSender, "Invalid 'from' address");
	default:
		return g_L10n.Translate("Unknown error");
	}
#undef DEBUG_CASE
#undef CASE
}

/**
 * Convert a gloox connection error enum to string
 * Keep in sync with Gloox documentation
 *
 * @param err Error to be converted
 * @return Converted error string
 */
std::string XmppClient::ConnectionErrorToString(gloox::ConnectionError err)
{
#define CASE(X, Y) case gloox::X: return Y
#define DEBUG_CASE(X, Y) case gloox::X: return g_L10n.Translate("Error") + " (" + Y + ")"
	switch (err)
	{
	CASE(ConnNoError, g_L10n.Translate("No error"));
	CASE(ConnStreamError, g_L10n.Translate("Stream error"));
	CASE(ConnStreamVersionError, g_L10n.Translate("The incoming stream version is unsupported"));
	CASE(ConnStreamClosed, g_L10n.Translate("The stream has been closed by the server"));
	DEBUG_CASE(ConnProxyAuthRequired, "The HTTP/SOCKS5 proxy requires authentication");
	DEBUG_CASE(ConnProxyAuthFailed, "HTTP/SOCKS5 proxy authentication failed");
	DEBUG_CASE(ConnProxyNoSupportedAuth, "The HTTP/SOCKS5 proxy requires an unsupported authentication mechanism");
	CASE(ConnIoError, g_L10n.Translate("An I/O error occurred"));
	DEBUG_CASE(ConnParseError, "An XML parse error occurred");
	CASE(ConnConnectionRefused, g_L10n.Translate("The connection was refused by the server"));
	CASE(ConnDnsError, g_L10n.Translate("Resolving the server's hostname failed"));
	CASE(ConnOutOfMemory, g_L10n.Translate("This system is out of memory"));
	DEBUG_CASE(ConnNoSupportedAuth, "The authentication mechanisms the server offered are not supported or no authentication mechanisms were available");
	CASE(ConnTlsFailed, g_L10n.Translate("The server's certificate could not be verified or the TLS handshake did not complete successfully"));
	CASE(ConnTlsNotAvailable, g_L10n.Translate("The server did not offer required TLS encryption"));
	DEBUG_CASE(ConnCompressionFailed, "Negotiation/initializing compression failed");
	CASE(ConnAuthenticationFailed, g_L10n.Translate("Authentication failed. Incorrect password or account does not exist"));
	CASE(ConnUserDisconnected, g_L10n.Translate("The user or system requested a disconnect"));
	CASE(ConnNotConnected, g_L10n.Translate("There is no active connection"));
	default:
		return g_L10n.Translate("Unknown error");
	}
#undef DEBUG_CASE
#undef CASE
}

/**
 * Convert a gloox registration result enum to string
 * Keep in sync with Gloox documentation
 *
 * @param err Enum to be converted
 * @return Converted string
 */
std::string XmppClient::RegistrationResultToString(gloox::RegistrationResult res)
{
#define CASE(X, Y) case gloox::X: return Y
#define DEBUG_CASE(X, Y) case gloox::X: return g_L10n.Translate("Error") + " (" + Y + ")"
	switch (res)
	{
	CASE(RegistrationSuccess, g_L10n.Translate("Your account has been successfully registered"));
	CASE(RegistrationNotAcceptable, g_L10n.Translate("Not all necessary information provided"));
	CASE(RegistrationConflict, g_L10n.Translate("Username already exists"));
	DEBUG_CASE(RegistrationNotAuthorized, "Account removal timeout or insufficiently secure channel for password change");
	DEBUG_CASE(RegistrationBadRequest, "Server received an incomplete request");
	DEBUG_CASE(RegistrationForbidden, "Registration forbidden");
	DEBUG_CASE(RegistrationRequired, "Account cannot be removed as it does not exist");
	DEBUG_CASE(RegistrationUnexpectedRequest, "This client is unregistered with the server");
	DEBUG_CASE(RegistrationNotAllowed, "Server does not permit password changes");
	default:
		return "";
	}
#undef DEBUG_CASE
#undef CASE
}

void XmppClient::SendStunEndpointToHost(const std::string& ip, u16 port, const std::string& hostJIDStr)
{
	DbgXMPP("SendStunEndpointToHost " << hostJIDStr);

	gloox::JID hostJID(hostJIDStr);
	gloox::Jingle::Session* session = m_sessionManager->createSession(hostJID,  this);

	gloox::Jingle::ICEUDP::CandidateList candidateList;

	candidateList.push_back(gloox::Jingle::ICEUDP::Candidate{
		"1", // component_id,
		"1", // foundation
		"0", // candidate_generation
		"1", // candidate_id
		ip,
		"0", // network
		port,
		0, // priority
		"udp",
		"", // base_ip
		0, // base_port
		gloox::Jingle::ICEUDP::ServerReflexive});

	// sessionInitiate deletes the new Content, and
	// the Plugin destructor inherited by Content frees the ICEUDP plugin.

	gloox::Jingle::PluginList pluginList;
	pluginList.push_back(new gloox::Jingle::ICEUDP(/*local_pwd*/ "", /*local_ufrag*/ "", candidateList));

	session->sessionInitiate(new gloox::Jingle::Content(std::string("game-data"), pluginList));
}

void XmppClient::handleSessionAction(gloox::Jingle::Action action, gloox::Jingle::Session* session, const gloox::Jingle::Session::Jingle* jingle)
{
	if (action == gloox::Jingle::SessionInitiate)
		handleSessionInitiation(session, jingle);
}

void XmppClient::handleSessionInitiation(gloox::Jingle::Session*,
	const gloox::Jingle::Session::Jingle* jingle)
{
	gloox::Jingle::ICEUDP::Candidate candidate{};

	const gloox::Jingle::Content* content = static_cast<const gloox::Jingle::Content*>(jingle->plugins().front());
	if (content)
	{
		const gloox::Jingle::ICEUDP* iceUDP = static_cast<const gloox::Jingle::ICEUDP*>(content->findPlugin(gloox::Jingle::PluginICEUDP));
		if (iceUDP)
			candidate = iceUDP->candidates().front();
	}

	if (candidate.ip.empty())
	{
		LOGERROR("Failed to retrieve Jingle candidate");
		return;
	}

	if (!g_NetServer)
	{
		LOGERROR("Received STUN connection request, but not hosting currently!");
		return;
	}

	g_NetServer->SendHolePunchingMessage(candidate.ip, candidate.port);
}
