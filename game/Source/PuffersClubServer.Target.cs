using UnrealBuildTool;
using System.Collections.Generic;

public class PuffersClubServerTarget : TargetRules
{
	public PuffersClubServerTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Server;
		DefaultBuildSettings = BuildSettingsVersion.V5;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("PuffersClub");
	}
}
