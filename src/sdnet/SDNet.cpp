// Copyright (C) 2007 Id Software, Inc.
//
// Offline network-service implementation used by the reconstructed demo
// engine.  The retail Demonware service remains a separate recovery area.

#include "../idlib/precompiled.h"
#pragma hdrstop

#include "SDNet.h"
#include "SDNetSessionManager.h"
#if !defined( SD_DEMO_BUILD ) || defined( SD_RETAIL_SDNET_ABI )
#include "SDNetStatsManager.h"
#include "SDNetFriendsManager.h"
#include "SDNetTeamManager.h"
#endif

namespace {

class sdNetSessionManagerOffline : public sdNetSessionManager {
public:
	sdNetSessionManagerOffline() {
		memset( &currentAddress, 0, sizeof( currentAddress ) );
		currentAddress.type = NA_BAD;
	}

	virtual const netadr_t& GetCurrentSessionAddress() const { return currentAddress; }
	virtual sdNetSession* AllocSession( const netadr_t* ) { return NULL; }
	virtual void FreeSession( sdNetSession* ) {}
	virtual sdNetTask* CreateSession( sdNetSession& ) { return NULL; }
	virtual sdNetTask* UpdateSession( sdNetSession& ) { return NULL; }
	virtual sdNetTask* DeleteSession( sdNetSession& ) { return NULL; }
	virtual sdNetTask* FindSessions( idList< sdNetSession* >&, sessionSource_e ) { return NULL; }
	virtual sdNetTask* RefreshSessions( idList< sdNetSession* >& ) { return NULL; }
	virtual sdNetTask* RefreshSession( sdNetSession& ) { return NULL; }

private:
	netadr_t currentAddress;
};

#if !defined( SD_DEMO_BUILD ) || defined( SD_RETAIL_SDNET_ABI )
class sdNetStatsManagerOffline : public sdNetStatsManager {
public:
	virtual bool WriteDictionary( const sdNetClientId&, const sdNetStatKeyValList& ) {
		return false;
	}

	virtual bool ReadCachedDictionary( const sdNetClientId&, sdNetStatKeyValList& ) {
		return false;
	}

	virtual sdNetTask* Flush() {
		return NULL;
	}

	virtual sdNetTask* ReadDictionary( const sdNetClientId&, sdNetStatKeyValList& ) {
		return NULL;
	}
};

class sdNetFriendsManagerOffline : public sdNetFriendsManager {
public:
	virtual const sdNetFriendsList& GetFriendsList() const {
		return friends;
	}

	virtual const sdNetFriendsList& GetPendingFriendsList() const {
		return pendingFriends;
	}

	virtual const sdNetFriendsList& GetInvitedFriendsList() const {
		return invitedFriends;
	}

	virtual const sdNetFriendsList& GetBlockedList() const {
		return blockedFriends;
	}

	virtual sdNetFriend* FindFriend( const sdNetFriendsList&, const char* ) {
		return NULL;
	}

	virtual int FindFriendIndex( const sdNetFriendsList&, const char* ) {
		return -1;
	}

	virtual sdLock& GetLock() {
		return lock;
	}

	virtual sdNetTask* Init() {
		return NULL;
	}

	virtual sdNetTask* ProposeFriendship( const char*, const wchar_t* ) {
		return NULL;
	}

	virtual sdNetTask* WithdrawProposal( const char* ) {
		return NULL;
	}

	virtual sdNetTask* AcceptProposal( const char* ) {
		return NULL;
	}

	virtual sdNetTask* RejectProposal( const char* ) {
		return NULL;
	}

	virtual sdNetTask* RemoveFriend( const char* ) {
		return NULL;
	}

	virtual sdNetTask* SetBlockedStatus( const char*, const sdNetFriend::blockState_e ) {
		return NULL;
	}

	virtual sdNetTask* SendMessage( const char*, const wchar_t* ) {
		return NULL;
	}

	virtual sdNetTask* Invite( const char*, const netadr_t& ) {
		return NULL;
	}

private:
	sdNetFriendsList friends;
	sdNetFriendsList pendingFriends;
	sdNetFriendsList invitedFriends;
	sdNetFriendsList blockedFriends;
	sdLock lock;
};

class sdNetTeamManagerOffline : public sdNetTeamManager {
public:
	virtual bool IsTeamMember() const {
		return false;
	}

	virtual const char* GetTeamName() const {
		return "";
	}

	virtual const sdNetTeamMemberList& GetMemberList() const {
		return members;
	}

	virtual const sdNetTeamMemberList& GetPendingMemberList() const {
		return pendingMembers;
	}

	virtual const sdNetTeamMemberList& GetPendingInvitesList() const {
		return pendingInvites;
	}

	virtual const sdNetTeamMember::memberStatus_e GetMemberStatus() const {
		return sdNetTeamMember::MS_MEMBER;
	}

	virtual sdNetTeamMember* FindMember( const sdNetTeamMemberList&, const char* ) {
		return NULL;
	}

	virtual int FindMemberIndex( const sdNetTeamMemberList&, const char* ) {
		return -1;
	}

	virtual sdLock& GetLock() {
		return lock;
	}

	virtual sdNetTask* Init() {
		return NULL;
	}

	virtual sdNetTask* CreateTeam( const char* ) {
		return NULL;
	}

	virtual sdNetTask* ProposeMembership( const char*, const wchar_t* ) {
		return NULL;
	}

	virtual sdNetTask* WithdrawMembership( const char* ) {
		return NULL;
	}

	virtual sdNetTask* AcceptMembership( const char*, const sdNetTeamId& ) {
		return NULL;
	}

	virtual sdNetTask* RejectMembership( const char*, const sdNetTeamId& ) {
		return NULL;
	}

	virtual sdNetTask* RemoveMember( const char* ) {
		return NULL;
	}

	virtual sdNetTask* SendMessage( const char*, const wchar_t* ) {
		return NULL;
	}

	virtual sdNetTask* BroadcastMessage( const wchar_t* ) {
		return NULL;
	}

	virtual sdNetTask* Invite( const char*, const netadr_t& ) {
		return NULL;
	}

	virtual sdNetTask* PromoteMember( const char* ) {
		return NULL;
	}

	virtual sdNetTask* DemoteMember( const char* ) {
		return NULL;
	}

	virtual sdNetTask* TransferOwnership( const char* ) {
		return NULL;
	}

	virtual sdNetTask* DisbandTeam() {
		return NULL;
	}

	virtual sdNetTask* LeaveTeam( const char* ) {
		return NULL;
	}

private:
	sdNetTeamMemberList members;
	sdNetTeamMemberList pendingMembers;
	sdNetTeamMemberList pendingInvites;
	sdLock lock;
};
#endif

class sdNetServiceOffline : public sdNetService {
public:
	sdNetServiceOffline() :
		state( SS_DISABLED ),
		disconnectReason( DR_NONE ),
		dedicatedState( DS_OFFLINE ),
		lastError( SDNET_NO_ERROR ) {
	}

	virtual bool Init() {
		state = SS_INITIALIZED;
		disconnectReason = DR_NONE;
		lastError = SDNET_NO_ERROR;
		return true;
	}

	virtual void Shutdown() {
		state = SS_DISABLED;
		dedicatedState = DS_OFFLINE;
	}

	virtual void RunFrame() {}
	virtual serviceState_e GetState() const { return state; }
	virtual disconnectReason_e GetDisconnectReason() const { return disconnectReason; }
	virtual dedicatedState_e GetDedicatedServerState() const { return dedicatedState; }
	virtual const motdList_t& GetMotD() const { return motd; }
	virtual bool CheckKey( const char*, bool ) const { return true; }
	virtual const char* GetStoredLicenseCode() const { return ""; }
	virtual bool IsSteamActive() const { return false; }

	virtual sdNetErrorCode_e CreateUser( sdNetUser** user, const char* ) {
		if ( user != NULL ) {
			*user = NULL;
		}
		lastError = SDNET_SERVICE_UNAVAILABLE;
		return lastError;
	}

	virtual void DeleteUser( sdNetUser* ) {}
	virtual int NumUsers() const { return 0; }
	virtual sdNetUser* GetUser( const int ) { return NULL; }
	virtual sdNetUser* GetActiveUser() { return NULL; }
	virtual sdNetSessionManager& GetSessionManager() { return sessionManager; }
#if !defined( SD_DEMO_BUILD ) || defined( SD_RETAIL_SDNET_ABI )
	virtual sdNetStatsManager& GetStatsManager() { return statsManager; }
	virtual sdNetFriendsManager& GetFriendsManager() { return friendsManager; }
	virtual sdNetTeamManager& GetTeamManager() { return teamManager; }
#endif
	virtual void FreeTask( sdNetTask* ) {}
	virtual sdNetErrorCode_e GetLastError() const { return lastError; }
	virtual sdNetTask* Connect() {
		lastError = SDNET_SERVICE_UNAVAILABLE;
		return NULL;
	}
	virtual sdNetTask* SignInDedicated() {
		lastError = SDNET_SERVICE_UNAVAILABLE;
		return NULL;
	}
	virtual sdNetTask* SignOutDedicated() {
		dedicatedState = DS_OFFLINE;
		return NULL;
	}
#if !defined( SD_DEMO_BUILD ) || defined( SD_RETAIL_SDNET_ABI )
	virtual sdNetTask* GetAccountsForLicense( idStrList& accountNames, const char* ) {
		accountNames.Clear();
		lastError = SDNET_SERVICE_UNAVAILABLE;
		return NULL;
	}

	virtual const idDict* GetProfileProperties( sdNetClientId ) const {
		return NULL;
	}
#endif

private:
	serviceState_e state;
	disconnectReason_e disconnectReason;
	dedicatedState_e dedicatedState;
	sdNetErrorCode_e lastError;
	motdList_t motd;
	sdNetSessionManagerOffline sessionManager;
#if !defined( SD_DEMO_BUILD ) || defined( SD_RETAIL_SDNET_ABI )
	sdNetStatsManagerOffline statsManager;
	sdNetFriendsManagerOffline friendsManager;
	sdNetTeamManagerOffline teamManager;
#endif
};

sdNetServiceOffline networkServiceOffline;

}

sdNetService* networkService = &networkServiceOffline;
