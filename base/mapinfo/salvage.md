
#include <mapinfo/mapinfo.include>

mapInfoDef "salvage" {
	_default_mapinfo( "salvage" )

	data {
		"script_entrypoint"		"Salvage_MapScript"
		"mapBriefing"			"maps/salvage/briefing"
		"mapLocation"			"maps/salvage/location"
		"campaignDescription"		"maps/salvage/campaign"
		"numObjectives"			"3"
		"mtr_serverShot"		"levelshots/salvage"
		"mtr_backdrop"			"levelshots/campaigns/europe"
		"mapPosition"			"512 93"
		"snd_music"				"sounds/music/load3"
		"strogg_endgame_pause"	"6.0"
		"gdf_endgame_pause"		"5.0"
	}
}

// =================================
// TEST MAPS
// =================================
/*
mapMetaData "maps/torchy/salvage_test" {
	"mapinfo"			"salvage"
	"dz_deployInfo"			"salvage"
	"size0"	"2096954518"
	"size1"	"2096954518"
	"size2"	"2096954518"
	"climate_skins"			"climate_skins_arctic"
}
*/