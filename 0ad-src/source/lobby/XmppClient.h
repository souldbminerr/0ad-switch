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

#ifndef XXXMPPCLIENT_H
#define XXXMPPCLIENT_H

#include "IXmppClient.h"

#include "lib/code_annotation.h"
#include "lib/external_libraries/gloox.h"
#include "lib/types.h"

#include <ctime>
#include <deque>
#include <js/RootingAPI.h>
#include <js/TypeDecls.h>
#include <js/Value.h>
#include <map>
#include <string>
#include <vector>

class JSTracer;
class ScriptInterface;

class XmppClient : public IXmppClient, public gloox::ConnectionListener, public gloox::MUCRoomHandler, public gloox::IqHandler, public gloox::RegistrationHandler, public gloox::MessageHandler, public gloox::Jingle::SessionHandler, public gloox::LogHandler
{
	NONCOPYABLE(XmppClient);

private:
	// Components
	gloox::Client* m_client;
	gloox::MUCRoom* m_mucRoom;
	gloox::Registration* m_registration;
	gloox::Jingle::SessionManager* m_sessionManager;

	// Account infos
	std::string m_username;
	std::string m_password;
	std::string m_server;
	std::string m_room;
	std::string m_nick;
	std::string m_xpartamuppId;
	std::string m_echelonId;

	// Security
	std::string m_connectionDataJid;
	std::string m_connectionDataIqId;

	// State
	gloox::CertStatus m_certStatus;
	bool m_initialLoadComplete;
	bool m_isConnected;
	bool m_regOpt;

public:
	// Basic
	XmppClient(const ScriptInterface* scriptInterface, const std::string& sUsername, const std::string& sPassword, const std::string& sRoom, const std::string& sNick, const int historyRequestSize = 0, const bool regOpt = false);
	virtual ~XmppClient();

	// JS::Heap is better for GC performance than JS::PersistentRooted
	static void Trace(JSTracer *trc, void *data)
	{
		static_cast<XmppClient*>(data)->TraceMember(trc);
	}

	void TraceMember(JSTracer *trc);

	// Network
	void connect() override;
	void disconnect() override;
	bool isConnected() override;
	void recv() override;
	void SendIqGetBoardList() override;
	void SendIqGetProfile(const std::string& player) override;
	void SendIqGameReport(const ScriptRequest& rq, JS::HandleValue data) override;
	void SendIqRegisterGame(const ScriptRequest& rq, JS::HandleValue data) override;
	void SendIqGetConnectionData(const std::string& jid, const std::string& password, const std::string& clientSalt, bool localIP) override;
	void SendIqUnregisterGame() override;
	void SendIqChangeStateGame(const std::string& nbp, const std::string& players) override;
	void SendIqLobbyAuth(const std::string& to, const std::string& token) override;
	void SetNick(const std::string& nick) override;
	std::string GetNick() const override;
	std::string GetJID() const override;
	std::string GetUsername() const override;
	void ChangePassword(const std::string& newPassword) override;
	void kick(const std::string& nick, const std::string& reason) override;
	void ban(const std::string& nick, const std::string& reason) override;
	void SetPresence(const std::string& presence) override;
	const char* GetPresence(const std::string& nickname) override;
	const char* GetRole(const std::string& nickname) override;
	std::wstring GetRating(const std::string& nickname) override;
	const std::wstring& GetSubject() override;

	JS::Value GUIGetPlayerList(const ScriptRequest& rq) override;
	JS::Value GUIGetGameList(const ScriptRequest& rq) override;
	JS::Value GUIGetBoardList(const ScriptRequest& rq) override;
	JS::Value GUIGetProfile(const ScriptRequest& rq) override;

	void SendStunEndpointToHost(const std::string& ip, u16 port, const std::string& hostJID) override;

	/**
	 * Convert gloox values to string or time.
	 */
	static const char* GetPresenceString(const gloox::Presence::PresenceType presenceType);
	static const char* GetRoleString(const gloox::MUCRoomRole role);
	static std::string StanzaErrorToString(gloox::StanzaError err);
	static std::string RegistrationResultToString(gloox::RegistrationResult res);
	static std::string ConnectionErrorToString(gloox::ConnectionError err);
	static std::string CertificateErrorToString(gloox::CertStatus status);
	static std::time_t ComputeTimestamp(const gloox::Message& msg);

protected:
	/* Xmpp handlers */
	/* MUC handlers */
	void handleMUCParticipantPresence(gloox::MUCRoom* room, gloox::MUCRoomParticipant, const gloox::Presence&) override;
	void handleMUCError(gloox::MUCRoom* room, gloox::StanzaError) override;
	void handleMUCMessage(gloox::MUCRoom* room, const gloox::Message& msg, bool priv) override;
	void handleMUCSubject(gloox::MUCRoom* room, const std::string& nick, const std::string& subject) override;
	// Currently unused, provide noop implemtation for pure virtual functions.
	bool handleMUCRoomCreation(gloox::MUCRoom*) override { return false; }
	void handleMUCInviteDecline(gloox::MUCRoom*, const gloox::JID&, const std::string&) override {}
	void handleMUCInfo(gloox::MUCRoom*, int, const std::string&, const gloox::DataForm*) override {}
	void handleMUCItems(gloox::MUCRoom*, const std::list<gloox::Disco::Item*, std::allocator<gloox::Disco::Item*> >&) override {}

	/* Log handler */
	void handleLog(gloox::LogLevel level, gloox::LogArea area, const std::string& message) override;

	/* ConnectionListener handlers*/
	void onConnect() override;
	void onDisconnect(gloox::ConnectionError e) override;
	bool onTLSConnect(const gloox::CertInfo& info) override;

	/* Iq Handlers */
	bool handleIq(const gloox::IQ& iq) override;
	void handleIqID(const gloox::IQ&, int) override {}

	/* Registration Handlers */
	void handleRegistrationFields(const gloox::JID& /*from*/, int fields, std::string instructions ) override;
#if GLOOXVERSION >= 0x010100
	void handleRegistrationResult(const gloox::JID& /*from*/, gloox::RegistrationResult result, const gloox::Error* /*error*/) override;
#else
	void handleRegistrationResult(const gloox::JID& /*from*/, gloox::RegistrationResult result) override;
#endif
	void handleAlreadyRegistered(const gloox::JID& /*from*/) override;
	void handleDataForm(const gloox::JID& /*from*/, const gloox::DataForm& /*form*/) override;
	void handleOOB(const gloox::JID& /*from*/, const gloox::OOB& oob) override;

	/* Message Handler */
	void handleMessage(const gloox::Message& msg, gloox::MessageSession* session) override;

	/* Session Handler */
	void handleSessionAction(gloox::Jingle::Action action, gloox::Jingle::Session* session, const gloox::Jingle::Session::Jingle* jingle) override;
	void handleSessionActionError(gloox::Jingle::Action /*action*/, gloox::Jingle::Session* /*session*/, const gloox::Error* /*error*/) override {}
	void handleIncomingSession(gloox::Jingle::Session* /*session*/) override {}
private:
	void handleSessionInitiation(gloox::Jingle::Session* session, const gloox::Jingle::Session::Jingle* jingle);

public:
	JS::Value GuiPollNewMessages(const ScriptInterface& guiInterface) override;
	JS::Value GuiPollHistoricMessages(const ScriptInterface& guiInterface) override;
	bool GuiPollHasPlayerListUpdate() override;
	void SendMUCMessage(const std::string& message) override;

protected:
	template<typename... Args>
	void CreateGUIMessage(
		const std::string& type,
		const std::string& level,
		const std::time_t time,
		Args const&... args);

private:
	struct SPlayer {
		SPlayer(const gloox::Presence::PresenceType presence, const gloox::MUCRoomRole role, const std::string& rating)
		: m_Presence(presence), m_Role(role), m_Rating(rating)
		{
		}
		gloox::Presence::PresenceType m_Presence;
		gloox::MUCRoomRole m_Role;
		std::string m_Rating;
	};
	using PlayerMap = std::map<std::string, SPlayer>;

	/// Map of players
	PlayerMap m_PlayerMap;
	/// Whether or not the playermap has changed since the last time the GUI checked.
	bool m_PlayerMapUpdate;
	/// List of games
	std::vector<const gloox::Tag*> m_GameList;
	/// List of rankings
	std::vector<const gloox::Tag*> m_BoardList;
	/// Profile data
	std::vector<const gloox::Tag*> m_Profile;
	/// ScriptInterface to root the values
	const ScriptInterface* m_ScriptInterface;
	/// Queue of messages for the GUI
	std::deque<JS::Heap<JS::Value> > m_GuiMessageQueue;
	/// Cache of all GUI messages received since the login
	std::vector<JS::Heap<JS::Value> > m_HistoricGuiMessages;
	/// Current room subject/topic.
	std::wstring m_Subject;
};

#endif // XMPPCLIENT_H
