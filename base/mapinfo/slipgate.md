
#include <mapinfo/mapinfo.include>

mapInfoDef "slipgate" {
	_default_mapinfo( "slipgate" )

	data {
		"script_entrypoint"		"Slipgate_MapScript"
		"mapBriefing"			"maps/slipgate/briefing"
		"mapLocation"			"maps/slipgate/location"
		"megatexture1"			"megatextures/slipgate"
		"megatexture2"			"megatextures/slipgate_snow"
		"campaignDescription"	"maps/slipgate/campaign"
		"numObjectives"			"4"
		"mtr_serverShot"		"levelshots/slipgate"
		"mtr_backdrop"			"levelshots/campaigns/africa"
		"mapPosition"			"463 93"
		"snd_music"				"sounds/music/load2"
		"strogg_endgame_pause"	"6.0"
		"gdf_endgame_pause"		"5.0"
	}
}

// =================================
// TEST MAPS
// =================================
/*
mapMetaData "maps/nkd/slipgate_objtest" {
	"mapinfo"				"slipgate"
	"dz_deployInfo"			"slipgate"
}
*/