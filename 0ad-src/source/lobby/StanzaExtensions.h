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
#ifndef STANZAEXTENSIONS_H
#define STANZAEXTENSIONS_H

#include "lib/external_libraries/gloox.h"

#include <string>
#include <vector>

/// Global Gamelist Extension
#define EXTGAMELISTQUERY 1403
#define XMLNS_GAMELIST "jabber:iq:gamelist"

/// Global Boardlist Extension
#define EXTBOARDLISTQUERY 1404
#define XMLNS_BOARDLIST "jabber:iq:boardlist"

/// Global Gamereport Extension
#define EXTGAMEREPORT 1405
#define XMLNS_GAMEREPORT "jabber:iq:gamereport"

/// Global Profile Extension
#define EXTPROFILEQUERY 1406
#define XMLNS_PROFILE "jabber:iq:profile"

/// Global Lobby Authentication Extension
#define EXTLOBBYAUTH 1407
#define XMLNS_LOBBYAUTH "jabber:iq:lobbyauth"

#define EXTCONNECTIONDATA 1408
#define XMLNS_CONNECTIONDATA "jabber:iq:connectiondata"

class ConnectionData : public gloox::StanzaExtension
{
public:
	ConnectionData(const gloox::Tag* tag = 0);

	// Following four methods are all required by gloox
	StanzaExtension* newInstance(const gloox::Tag* tag) const override
	{
		return new ConnectionData(tag);
	}
	const std::string& filterString() const override;
	gloox::Tag* tag() const override;
	gloox::StanzaExtension* clone() const override;

	std::string m_Ip;
	std::string m_Port;
	std::string m_IsLocalIP;
	std::string m_Password;
	std::string m_ClientSalt;
	std::string m_Error;
};

class GameReport : public gloox::StanzaExtension
{
public:
	GameReport(const gloox::Tag* tag = 0);

	// Following four methods are all required by gloox
	StanzaExtension* newInstance(const gloox::Tag* tag) const override
	{
		return new GameReport(tag);
	}
	const std::string& filterString() const override;
	gloox::Tag* tag() const override;
	gloox::StanzaExtension* clone() const override;

	std::vector<const gloox::Tag*> m_GameReport;
};

class GameListQuery : public gloox::StanzaExtension
{
public:
	GameListQuery(const gloox::Tag* tag = 0);

	// Following four methods are all required by gloox
	StanzaExtension* newInstance(const gloox::Tag* tag) const override
	{
		return new GameListQuery(tag);
	}
	const std::string& filterString() const override;
	gloox::Tag* tag() const override;
	gloox::StanzaExtension* clone() const override;

	~GameListQuery();

	std::string m_Command;
	std::vector<const gloox::Tag*> m_GameList;
};

class BoardListQuery : public gloox::StanzaExtension
{
public:
	BoardListQuery(const gloox::Tag* tag = 0);

	// Following four methods are all required by gloox
	StanzaExtension* newInstance(const gloox::Tag* tag) const override
	{
		return new BoardListQuery(tag);
	}
	const std::string& filterString() const override;
	gloox::Tag* tag() const override;
	gloox::StanzaExtension* clone() const override;

	~BoardListQuery();

	std::string m_Command;
	std::vector<const gloox::Tag*> m_StanzaBoardList;
};

class ProfileQuery : public gloox::StanzaExtension
{
public:
	ProfileQuery(const gloox::Tag* tag = 0);

	// Following four methods are all required by gloox
	StanzaExtension* newInstance(const gloox::Tag* tag) const override
	{
		return new ProfileQuery(tag);
	}
	const std::string& filterString() const override;
	gloox::Tag* tag() const override;
	gloox::StanzaExtension* clone() const override;

	~ProfileQuery();

	std::string m_Command;
	std::vector<const gloox::Tag*> m_StanzaProfile;
};

class LobbyAuth : public gloox::StanzaExtension
{
public:
	LobbyAuth(const gloox::Tag* tag = 0);

	// Following four methods are all required by gloox
	StanzaExtension* newInstance(const gloox::Tag* tag) const override
	{
		return new LobbyAuth(tag);
	}
	const std::string& filterString() const override;
	gloox::Tag* tag() const override;
	gloox::StanzaExtension* clone() const override;

	std::string m_Token;
};
#endif // STANZAEXTENSIONS_H
