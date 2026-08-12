#include <quickchat/common.include>

// MEDIC CHATS ////////////////////////////////////////////

quickChatDef strogg/botchat/generic/holdonimatechnician {
	_chat( "strogg/botchat/generic/imatechnician", "", "team", BOT_TYPE_MEDIC_ACK, "" )
}

quickChatDef strogg/botchat/generic/technicianenroute {
	_chat( "strogg/botchat/generic/technicianenroute", "", "team", BOT_TYPE_MEDIC_ACK, "" )
}

quickChatDef strogg/botchat/generic/tkrevive {
	_chat( "strogg/botchat/generic/tkrevive", "", "team", BOT_TYPE_TK_REVIVE, "" )
}

// ESCORT CHATS ////////////////////////////////////////

quickChatDef strogg/botchat/generic/youlead {
	_chat( "strogg/botchat/generic/youlead", "", "team", BOT_TYPE_LETS_GO, "" )
}

quickChatDef strogg/botchat/generic/gotyourback {
	_chat( "strogg/botchat/generic/gotyourback", "", "team", BOT_TYPE_LETS_GO, "" )
}

quickChatDef strogg/botchat/generic/leadway {
	_chat( "strogg/botchat/generic/leadway", "", "team", BOT_TYPE_LETS_GO, "" )
}

quickChatDef strogg/botchat/generic/whereto {
	_chat( "strogg/botchat/generic/whereto", "", "team", BOT_TYPE_LETS_GO, "" )
}

// ENGINEER CHATS ///////////////////////////////////

quickChatDef strogg/botchat/generic/vehicle/letmerepair {
	_chat( "strogg/botchat/generic/vehicle/letmerepair", "", "team", BOT_TYPE_WILL_FIX_RIDE, "" )
}

quickChatDef strogg/botchat/generic/vehicle/iwillfixride {
	_chat( "strogg/botchat/generic/vehicle/iwillfixride", "", "team", BOT_TYPE_WILL_FIX_RIDE, "" )
}

quickChatDef strogg/botchat/generic/vehicle/stopiwillfixride {
	_chat( "strogg/botchat/generic/vehicle/stopiwillfixride", "", "team", BOT_TYPE_STOP_WILL_FIX_RIDE, "" )
}


// AMMO CHAT ////////////////////////////////////////

quickChatDef strogg/botchat/generic/gotammo {
	_chat( "strogg/botchat/generic/gotammo", "", "team", BOT_TYPE_AMMO_ACK, "" )
}

// GENERIC OBJ CHATS //////////////////////////////////
quickChatDef strogg/botchat/objectives/plantplasmacharge {
	_chat( "strogg/botchat/objectives/explosives/plant", "", "team", BOT_TYPE_GENERIC_PLANT, "" )
}




// GENERIC CHATS //////////////////////////////////////
quickChatDef strogg/botchat/generic/needalift {
	_chat( "strogg/botchat/generic/needaride", "", "team", BOT_TYPE_NEED_LIFT, "" )
}

quickChatDef strogg/botchat/generic/needtransport {
	_chat( "strogg/botchat/generic/needatransport", "", "team", BOT_TYPE_NEED_LIFT, "" )
}



