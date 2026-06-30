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

#include "StanzaExtensions.h"

#include <list>

/******************************************************
 * GameReport, fairly generic custom stanza extension used
 * to report game statistics.
 */
GameReport::GameReport(const gloox::Tag* tag)
	: StanzaExtension(EXTGAMEREPORT)
{
	if (!tag || tag->name() != "report" || tag->xmlns() != XMLNS_GAMEREPORT)
		return;
	// TODO if we want to handle receiving this stanza extension.
};

/**
 * Required by gloox, used to serialize the GameReport into XML for sending.
 */
gloox::Tag* GameReport::tag() const
{
	gloox::Tag* t = new gloox::Tag("report");
	t->setXmlns(XMLNS_GAMEREPORT);

	for (const gloox::Tag* const& tag : m_GameReport)
		t->addChild(tag->clone());

	return t;
}

/**
 * Required by gloox, used to find the GameReport element in a recived IQ.
 */
const std::string& GameReport::filterString() const
{
	static const std::string filter = "/iq/report[@xmlns='" XMLNS_GAMEREPORT "']";
	return filter;
}

gloox::StanzaExtension* GameReport::clone() const
{
	GameReport* q = new GameReport();
	return q;
}

/******************************************************
 * BoardListQuery, a flexible custom IQ Stanza useful for anything with ratings, used to
 * request and receive leaderboard and rating data from server.
 * Example stanza:
 * <board player="foobar">1200</board>
 */
BoardListQuery::BoardListQuery(const gloox::Tag* tag)
	: StanzaExtension(EXTBOARDLISTQUERY)
{
	if (!tag || tag->name() != "query" || tag->xmlns() != XMLNS_BOARDLIST)
		return;

	const gloox::Tag* c = tag->findTag("query/command");
	if (c)
		m_Command = c->cdata();
	for (const gloox::Tag* const& t : tag->findTagList("query/board"))
		m_StanzaBoardList.emplace_back(t->clone());
}

/**
 * Required by gloox, used to find the BoardList element in a received IQ.
 */
const std::string& BoardListQuery::filterString() const
{
	static const std::string filter = "/iq/query[@xmlns='" XMLNS_BOARDLIST "']";
	return filter;
}

/**
 * Required by gloox, used to serialize the BoardList request into XML for sending.
 */
gloox::Tag* BoardListQuery::tag() const
{
	gloox::Tag* t = new gloox::Tag("query");
	t->setXmlns(XMLNS_BOARDLIST);

	// Check for ratinglist or boardlist command
	if (!m_Command.empty())
		t->addChild(new gloox::Tag("command", m_Command));

	for (const gloox::Tag* const& tag : m_StanzaBoardList)
		t->addChild(tag->clone());

	return t;
}

gloox::StanzaExtension* BoardListQuery::clone() const
{
	BoardListQuery* q = new BoardListQuery();
	return q;
}

BoardListQuery::~BoardListQuery()
{
	for (const gloox::Tag* const& t : m_StanzaBoardList)
		delete t;
	m_StanzaBoardList.clear();
}

/******************************************************
 * GameListQuery, custom IQ Stanza, used to receive
 * the listing of games from the server, and register/
 * unregister/changestate games on the server.
 */
GameListQuery::GameListQuery(const gloox::Tag* tag)
	: StanzaExtension(EXTGAMELISTQUERY)
{
	if (!tag || tag->name() != "query" || tag->xmlns() != XMLNS_GAMELIST)
		return;

	const gloox::Tag* c = tag->findTag("query/command");
	if (c)
		m_Command = c->cdata();

	for (const gloox::Tag* const& t : tag->findTagList("query/game"))
		m_GameList.emplace_back(t->clone());
}

/**
 * Required by gloox, used to find the GameList element in a received IQ.
 */
const std::string& GameListQuery::filterString() const
{
	static const std::string filter = "/iq/query[@xmlns='" XMLNS_GAMELIST "']";
	return filter;
}

/**
 * Required by gloox, used to serialize the game object into XML for sending.
 */
gloox::Tag* GameListQuery::tag() const
{
	gloox::Tag* t = new gloox::Tag("query");
	t->setXmlns(XMLNS_GAMELIST);

	// Check for register / unregister command
	if (!m_Command.empty())
		t->addChild(new gloox::Tag("command", m_Command));

	for (const gloox::Tag* const& tag : m_GameList)
		t->addChild(tag->clone());

	return t;
}

gloox::StanzaExtension* GameListQuery::clone() const
{
	GameListQuery* q = new GameListQuery();
	return q;
}

GameListQuery::~GameListQuery()
{
	for (const gloox::Tag* const & t : m_GameList)
		delete t;
	m_GameList.clear();
}

/******************************************************
 * ProfileQuery, a custom IQ Stanza useful for fetching
 * user profiles
 * Example stanza:
 * <profile player="foobar" highestRating="1500" rank="1895" totalGamesPlayed="50"
 * 	wins="25" losses="25" /><command>foobar</command>
 */
ProfileQuery::ProfileQuery(const gloox::Tag* tag)
	: StanzaExtension(EXTPROFILEQUERY)
{
	if (!tag || tag->name() != "query" || tag->xmlns() != XMLNS_PROFILE)
		return;

	const gloox::Tag* c = tag->findTag("query/command");
	if (c)
		m_Command = c->cdata();

	for (const gloox::Tag* const& t : tag->findTagList("query/profile"))
		m_StanzaProfile.emplace_back(t->clone());
}

/**
 * Required by gloox, used to find the Profile element in a received IQ.
 */
const std::string& ProfileQuery::filterString() const
{
	static const std::string filter = "/iq/query[@xmlns='" XMLNS_PROFILE "']";
	return filter;
}

/**
 * Required by gloox, used to serialize the Profile request into XML for sending.
 */
gloox::Tag* ProfileQuery::tag() const
{
	gloox::Tag* t = new gloox::Tag("query");
	t->setXmlns(XMLNS_PROFILE);

	if (!m_Command.empty())
		t->addChild(new gloox::Tag("command", m_Command));

	for (const gloox::Tag* const& tag : m_StanzaProfile)
		t->addChild(tag->clone());

	return t;
}

gloox::StanzaExtension* ProfileQuery::clone() const
{
	ProfileQuery* q = new ProfileQuery();
	return q;
}

ProfileQuery::~ProfileQuery()
{
	for (const gloox::Tag* const& t : m_StanzaProfile)
		delete t;
	m_StanzaProfile.clear();
}

/******************************************************
 * LobbyAuth, a custom IQ Stanza, used to send and
 * receive a security token for hosting authentication.
 */
LobbyAuth::LobbyAuth(const gloox::Tag* tag)
	: StanzaExtension(EXTLOBBYAUTH)
{
	if (!tag || tag->name() != "auth" || tag->xmlns() != XMLNS_LOBBYAUTH)
		return;

	const gloox::Tag* c = tag->findTag("auth/token");
	if (c)
		m_Token = c->cdata();
}

/**
 * Required by gloox, used to find the LobbyAuth element in a received IQ.
 */
const std::string& LobbyAuth::filterString() const
{
	static const std::string filter = "/iq/auth[@xmlns='" XMLNS_LOBBYAUTH "']";
	return filter;
}

/**
 * Required by gloox, used to serialize the auth object into XML for sending.
 */
gloox::Tag* LobbyAuth::tag() const
{
	gloox::Tag* t = new gloox::Tag("auth");
	t->setXmlns(XMLNS_LOBBYAUTH);

	// Check for the auth token
	if (!m_Token.empty())
		t->addChild(new gloox::Tag("token", m_Token));
	return t;
}

gloox::StanzaExtension* LobbyAuth::clone() const
{
	return new LobbyAuth();
}

/******************************************************
 * ConnectionData, a custom IQ Stanza, used to send and
 * receive a ip and port of the server.
 */
ConnectionData::ConnectionData(const gloox::Tag* tag)
	: StanzaExtension(EXTCONNECTIONDATA)
{
	if (!tag || tag->name() != "connectiondata" || tag->xmlns() != XMLNS_CONNECTIONDATA)
		return;

	const gloox::Tag* c = tag->findTag("connectiondata/ip");
	if (c)
		m_Ip = c->cdata();
	const gloox::Tag* p= tag->findTag("connectiondata/port");
	if (p)
		m_Port = p->cdata();
	const gloox::Tag* pip = tag->findTag("connectiondata/isLocalIP");
	if (pip)
		m_IsLocalIP = pip->cdata();
	const gloox::Tag* pw = tag->findTag("connectiondata/password");
	if (pw)
		m_Password = pw->cdata();
	const gloox::Tag* cs = tag->findTag("connectiondata/clientsalt");
	if (cs)
		m_ClientSalt = cs->cdata();
	const gloox::Tag* e = tag->findTag("connectiondata/error");
	if (e)
		m_Error= e->cdata();
}

/**
 * Required by gloox, used to find the LobbyAuth element in a received IQ.
 */
const std::string& ConnectionData::filterString() const
{
	static const std::string filter = "/iq/connectiondata[@xmlns='" XMLNS_CONNECTIONDATA "']";
	return filter;
}

/**
 * Required by gloox, used to serialize the auth object into XML for sending.
 */
gloox::Tag* ConnectionData::tag() const
{
	gloox::Tag* t = new gloox::Tag("connectiondata");
	t->setXmlns(XMLNS_CONNECTIONDATA);

	if (!m_Ip.empty())
		t->addChild(new gloox::Tag("ip", m_Ip));
	if (!m_Port.empty())
		t->addChild(new gloox::Tag("port", m_Port));
	if (!m_IsLocalIP.empty())
		t->addChild(new gloox::Tag("isLocalIP", m_IsLocalIP));
	if (!m_Password.empty())
		t->addChild(new gloox::Tag("password", m_Password));
	if (!m_ClientSalt.empty())
		t->addChild(new gloox::Tag("clientsalt", m_ClientSalt));
	if (!m_Error.empty())
		t->addChild(new gloox::Tag("error", m_Error));
	return t;
}

gloox::StanzaExtension* ConnectionData::clone() const
{
	return new ConnectionData();
}
