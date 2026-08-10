// Copyright (C) 2007 Id Software, Inc.

#include "../idlib/precompiled.h"
#pragma hdrstop

#include "NotificationSystem.h"

namespace {

class sdNotificationSystemLocal : public sdNotificationSystem {
public:
	virtual int GetNumNotifications() const { return 0; }
	virtual const notification_t* GetNotification( const int ) const { return NULL; }
};

sdNotificationSystemLocal notificationSystemLocal;

}

sdNotificationSystem* notificationSystem = &notificationSystemLocal;
