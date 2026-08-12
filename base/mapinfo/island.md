
#include <mapinfo/mapinfo.include>

mapInfoDef "island" {
	_default_mapinfo( "island" )

	data {
		"script_entrypoint"		"Island_MapScript"
		"mapBriefing"			"maps/island/briefing"
		"mapLocation"			"maps/island/location"
		"campaignDescription"	"maps/island/campaign"
		"numObjectives"			"3"
		"mtr_serverShot"		"levelshots/island"
		"mtr_serverShotThumb"	"levelshots/thumbs/island"
		"mtr_backdrop"			"levelshots/campaigns/africa"
		"mapPosition"			"441 193"
		"snd_music"				"sounds/music/load3"
		"strogg_endgame_pause"	"5.0"
		"gdf_endgame_pause"		"6.0"
	}
}
