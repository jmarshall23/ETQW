
#include <mapinfo/mapinfo.include>
/*
mapMetaData "maps/dm" {
	"mapinfo"				"dm"
	"dz_deployInfo"			"dm"
}
*/
mapInfoDef "dm" {
	_default_mapinfo( "dm" )

	data {
		"pretty_name"			"Death Match test map"
		"script_entrypoint"		"DM_MapScript"
		"numObjectives"			"0"		
	}
}