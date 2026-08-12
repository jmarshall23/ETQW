
#include <mapinfo/mapinfo.include>

mapInfoDef "sewer" {
	_default_mapinfo( "sewer" )

	data {
		"mapLocation"			"maps/sewer/location"
		"script_entrypoint"		"Sewer_MapScript"
		"mapBriefing"			"maps/sewer/briefing"
		"campaignDescription"	"maps/sewer/campaign"
		"numObjectives"			"3"
		"mtr_serverShot"		"levelshots/sewer"
		"mtr_backdrop"			"levelshots/campaigns/pacific"
		"mapPosition"			"387 102"
		"snd_music"				"sounds/music/load2"
		"strogg_endgame_pause"	"10.0"
		"gdf_endgame_pause"		"5.0"
	}
}
