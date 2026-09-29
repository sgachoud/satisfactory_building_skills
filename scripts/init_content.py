"""Run with UnrealEditor-Cmd -run=pythonscript; never overwrite existing assets."""
from pathlib import Path

import unreal


MOUNT = "/BuildingSkills"
TOOLS = unreal.AssetToolsHelpers.get_asset_tools()
# Commandlets can run before the background registry scan reaches this plugin.
unreal.AssetRegistryHelpers.get_asset_registry().scan_paths_synchronous(
    [MOUNT], force_rescan=True
)


def save(asset):
    if not unreal.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False):
        raise RuntimeError(f"Could not save {asset.get_path_name()}")


def create_feature():
    path = f"{MOUNT}/BuildingSkills"
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        feature = unreal.load_asset(path)
        if not isinstance(feature, unreal.GameFeatureData):
            raise RuntimeError(f"Unexpected asset type at {path}")
        return

    factory = unreal.DataAssetFactory()
    factory.set_editor_property("data_asset_class", unreal.GameFeatureData)
    feature = TOOLS.create_asset("BuildingSkills", MOUNT, unreal.GameFeatureData, factory)
    if feature is None:
        raise RuntimeError("Could not create BuildingSkills game feature data")

    # Use the installed Alpakit template, just as its Create Mod wizard does.
    template = (Path(unreal.Paths.project_dir()) / "Mods/Alpakit/Config/DefaultAlpakit.ini")
    asset_types = []
    for line in template.read_text(encoding="utf-8-sig").splitlines():
        if line.startswith("+PrimaryAssetTypeTemplates="):
            entry = unreal.PrimaryAssetTypeInfo()
            value = line.split("=", 1)[1].replace("$PluginName", "BuildingSkills")
            if not entry.import_text(value):
                raise RuntimeError(f"Could not parse Alpakit primary asset template: {value}")
            asset_types.append(entry)
    if not asset_types:
        raise RuntimeError("Installed Alpakit primary asset templates were not found")
    feature.set_editor_property("primary_asset_types_to_scan", asset_types)
    save(feature)


def create_root(name, parent):
    path = f"{MOUNT}/{name}"
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        blueprint = unreal.load_asset(path)
    else:
        factory = unreal.BlueprintFactory()
        factory.set_editor_property("parent_class", parent)
        blueprint = TOOLS.create_asset(name, MOUNT, unreal.Blueprint, factory)
        if blueprint is None:
            raise RuntimeError(f"Could not create {path}")
        defaults = unreal.get_default_object(blueprint.generated_class())
        defaults.set_editor_property("root_module", True)
        unreal.BlueprintEditorLibrary.compile_blueprint(blueprint)
        save(blueprint)

    if not isinstance(blueprint, unreal.Blueprint):
        raise RuntimeError(f"Unexpected asset type at {path}")
    defaults = unreal.get_default_object(blueprint.generated_class())
    if not isinstance(defaults, parent) or not defaults.get_editor_property("root_module"):
        raise RuntimeError(f"{path} must be a root module derived from {parent}")


create_feature()
create_root("RootGameInstance_BuildingSkills", unreal.GameInstanceModule)
create_root("RootGameWorld_BuildingSkills", unreal.GameWorldModule)
unreal.log("BuildingSkills content verified.")
