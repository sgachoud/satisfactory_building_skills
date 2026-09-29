"""Create/update only Building Skills' Edit input assets in the Unreal editor."""
import unreal


ROOT = "/BuildingSkills/Inputs"
ACTION_PATH = f"{ROOT}/IA_ToggleEditMode"
CONTEXT_PATH = f"{ROOT}/MC_BuildingSkills"
ALT_PATH = "/Game/FactoryGame/Inputs/IA_ChordAlt"
PARENT_PATH = "/Game/FactoryGame/Inputs/Player/MC_PlayerActions"
registry = unreal.AssetRegistryHelpers.get_asset_registry()
registry.scan_paths_synchronous(["/BuildingSkills", "/Game/FactoryGame/Inputs"], force_rescan=True)
tools = unreal.AssetToolsHelpers.get_asset_tools()


def get_or_create(path, asset_class):
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        asset = unreal.load_asset(path)
        if not isinstance(asset, asset_class):
            raise RuntimeError(f"Unexpected asset class at {path}")
        return asset
    folder, name = path.rsplit("/", 1)
    factory = unreal.DataAssetFactory()
    factory.set_editor_property("data_asset_class", asset_class)
    asset = tools.create_asset(name, folder, asset_class, factory)
    if asset is None:
        raise RuntimeError(f"Failed to create {path}")
    return asset


alt = unreal.load_asset(ALT_PATH)
parent = unreal.load_asset(PARENT_PATH)
if not isinstance(alt, unreal.InputAction) or not isinstance(parent, unreal.FGInputMappingContext):
    raise RuntimeError("The installed game's Alt chord or player-actions context is missing")

action = get_or_create(ACTION_PATH, unreal.InputAction)
action.set_editor_property("value_type", unreal.InputActionValueType.BOOLEAN)
action.set_editor_property("action_description", "Open or close the Build Gun's Edit mode.")
action.set_editor_property("consume_input", True)
action.set_editor_property("trigger_when_paused", False)
# Keep the action active throughout the chord, so the engine blocks vanilla E
# until it is released. The C++ callback handles one toggle per activation.
down = unreal.new_object(unreal.InputTriggerDown, outer=action)
action.set_editor_property("triggers", [down])
settings = action.get_editor_property("player_mappable_key_settings")
if settings is None:
    settings = unreal.new_object(unreal.PlayerMappableKeySettings, outer=action)
settings.set_editor_property("name", "BuildingSkills.ToggleEditMode")
settings.set_editor_property("display_name", "Open / Close Edit Mode")
settings.set_editor_property("display_category", "Building Skills")
settings.set_editor_property("is_player_mappable", True)
action.set_editor_property("player_mappable_key_settings", settings)

context = get_or_create(CONTEXT_PATH, unreal.FGChildInputMappingContext)
context.set_editor_property("m_parent_context", parent)
context.set_editor_property("m_display_name", "Building Skills")
context.set_editor_property("m_menu_priority", 200.0)
# ChordAction is EditInstanceOnly. Configure an instance before making it an
# asset-owned subobject, which Python otherwise treats as a template.
chord = unreal.new_object(unreal.InputTriggerChordAction)
chord.set_editor_property("chord_action", alt)
if not chord.rename(outer=context):
    raise RuntimeError("Could not attach the chord trigger to its mapping context")
key = unreal.Key()
key.set_editor_property("key_name", "E")
mapping = unreal.EnhancedActionKeyMapping(action=action, key=key, triggers=[chord])
context.set_editor_property("mappings", [mapping])

for asset in (action, context):
    if not unreal.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False):
        raise RuntimeError(f"Failed to save {asset.get_path_name()}")

feature = unreal.load_asset("/BuildingSkills/BuildingSkills")
scan_types = feature.get_editor_property("primary_asset_types_to_scan")
if not any(str(entry.get_editor_property("primary_asset_type")) == "FGChildInputMappingContext"
           for entry in scan_types):
    raise RuntimeError("The game feature must scan FGChildInputMappingContext assets in /BuildingSkills/Inputs")

unreal.log("BuildingSkills Edit input assets saved.")
