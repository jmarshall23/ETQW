
#include <mapinfo/mapinfo.include>

mapInfoDef "etqwmap" {
	_default_mapinfo( "etqwmap" )

	data {
		"mapLocation"			"maps/etqwmap/location"
		"script_entrypoint"		"EtqwMap_MapScript"
		"mapBriefing"			"maps/etqwmap/briefing"
		"campaignDescription"	"maps/etqwmap/campaign"
		"numObjectives"			"4"
		"mtr_serverShot"		"levelshots/etqwmap"
		"mtr_backdrop"			"levelshots/campaigns/northamerica"
		"mapPosition"			"311 140"
		"snd_music"				"sounds/music/load1"
		"strogg_endgame_pause"	"6.0"
		"gdf_endgame_pause"		"5.0"
	}
}
