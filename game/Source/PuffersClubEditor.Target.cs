using UnrealBuildTool;
using System.Collections.Generic;

public class PuffersClubEditorTarget : TargetRules
{
	public PuffersClubEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.V5;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("PuffersClub");
	}
}
