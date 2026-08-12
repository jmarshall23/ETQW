
#include <mapinfo/mapinfo.include>

mapInfoDef "refinery" {
	_default_mapinfo( "refinery" )
	
	data {
		"script_entrypoint"		"Refinery_MapScript"
		"mapBriefing"			"maps/refinery/briefing"
		"mapLocation"			"maps/refinery/location"
		"campaignDescription"	"maps/refinery/campaign"
		"numObjectives"			"3"
		"mtr_serverShot"		"levelshots/refinery"
		"mtr_backdrop"			"levelshots/campaigns/africa"
		"mapPosition"			"553 118"
		"snd_music"				"sounds/music/load1"
		"strogg_endgame_pause"	"5.0"
		"gdf_endgame_pause"		"10.0"
	}
}
