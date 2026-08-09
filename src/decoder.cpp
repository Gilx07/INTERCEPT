#include "decoder.hpp"

#include <sstream>

namespace {

const char* rpc_name(
    unsigned char id
)
{
    switch (id)
    {
    case 11:
        return "SetPlayerName";

    case 12:
        return "SetPlayerPos";

    case 13:
        return "SetPlayerPosFindZ";

    case 14:
        return "SetPlayerHealth";

    case 15:
        return "TogglePlayerControllable";

    case 16:
        return "PlaySound";

    case 17:
        return "SetWorldBounds";

    case 18:
        return "GivePlayerMoney";

    case 19:
        return "SetPlayerFacingAngle";

    case 20:
        return "ResetPlayerMoney";

    case 21:
        return "ResetPlayerWeapons";

    case 22:
        return "GivePlayerWeapon";

    case 24:
        return "SetVehicleParamsEx";

    case 25:
        return "ClientJoin";

    case 26:
        return "PlayerEnterVehicle";

    case 27:
        return "EnterEditObject / SelectObject";

    case 28:
        return "CancelEdit";

    case 29:
        return "SetPlayerTime";

    case 30:
        return "ToggleClock";

    case 32:
        return "WorldPlayerAdd";

    case 33:
        return "SetShopName";

    case 34:
        return "SetPlayerSkillLevel";

    case 35:
        return "SetPlayerDrunkLevel";

    case 36:
        return "Create3DTextLabel";

    case 37:
        return "DisableCheckpoint";

    case 38:
        return "SetRaceCheckpoint";

    case 39:
        return "DisableRaceCheckpoint";

    case 40:
        return "GameModeRestart";

    case 41:
        return "PlayAudioStream";

    case 42:
        return "StopAudioStream";

    case 43:
        return "RemoveBuilding";

    case 44:
        return "CreateObject";

    case 45:
        return "SetObjectPos";

    case 46:
        return "SetObjectRotation";

    case 47:
        return "DestroyObject";

    case 50:
        return "SendCommand";

    case 52:
        return "SendSpawn";

    case 53:
        return "DeathNotification";

    case 54:
        return "NPCJoin";

    case 55:
        return "SendDeathMessage";

    case 56:
        return "SetMapIcon";

    case 57:
        return "RemoveVehicleComponent";

    case 58:
        return "Delete3DTextLabel";

    case 59:
        return "ChatBubble";

    case 60:
        return "SendGameTimeUpdate";

    case 61:
        return "ShowDialog";

    case 62:
        return "DialogResponse";

    case 63:
        return "DestroyPickup";

    case 65:
        return "LinkVehicleToInterior";

    case 66:
        return "SetPlayerArmour";

    case 67:
        return "SetArmedWeapon";

    case 68:
        return "SetSpawnInfo";

    case 69:
        return "SetPlayerTeam";

    case 70:
        return "PutPlayerInVehicle";

    case 71:
        return "RemovePlayerFromVehicle";

    case 72:
        return "SetPlayerColor";

    case 73:
        return "ShowGameText";

    case 74:
        return "ForceClassSelection";

    case 75:
        return "AttachObjectToPlayer";

    case 76:
        return "InitMenu";

    case 77:
        return "ShowMenu";

    case 78:
        return "HideMenu";

    case 79:
        return "CreateExplosion";

    case 80:
        return "ShowPlayerNameTag";

    case 81:
        return "AttachCameraToObject";

    case 82:
        return "InterpolateCamera";

    case 83:
        return "ToggleSelectTextDraw / SelectTextDraw";

    case 84:
        return "SetPlayerObjectMaterial";

    case 85:
        return "GangZoneStopFlash";

    case 86:
        return "ApplyPlayerAnimation";

    case 87:
        return "ClearPlayerAnimation";

    case 88:
        return "SetPlayerSpecialAction";

    case 89:
        return "SetPlayerFightingStyle";

    case 90:
        return "SetPlayerVelocity";

    case 91:
        return "SetVehicleVelocity";

    case 93:
        return "SendClientMessage";

    case 94:
        return "SetWorldTime";

    case 95:
        return "CreatePickup";

    case 96:
        return "ScmEvent";

    case 98:
        return "SetVehicleTireStatus";

    case 99:
        return "MoveObject";

    case 103:
        return "ClientCheckResponse / ClientCheck";

    case 104:
        return "EnableStuntBonus";

    case 105:
        return "TextDrawSetString";

    case 106:
        return "VehicleDamaged";

    case 107:
        return "SetCheckpoint";

    case 108:
        return "AddGangZone";

    case 111:
        return "ToggleWidescreen";

    case 112:
        return "PlayCrimeReport";

    case 113:
        return "SetPlayerAttachedObject";

    case 115:
        return "GiveTakeDamage";

    case 116:
        return "EditAttachedObject";

    case 117:
        return "EditObject";

    case 118:
        return "InteriorChangeNotification";

    case 119:
        return "MapMarker";

    case 120:
        return "GangZoneDestroy";

    case 121:
        return "GangZoneFlash";

    case 122:
        return "StopObject";

    case 123:
        return "SetVehicleNumberPlate";

    case 124:
        return "TogglePlayerSpectating";

    case 126:
        return "SpectatePlayer";

    case 127:
        return "SpectateVehicle";

    case 128:
        return "RequestClass";

    case 129:
        return "RequestSpawn";

    case 130:
        return "ConnectionRejected";

    case 131:
        return "PickedUpPickup";

    case 132:
        return "MenuSelect";

    case 133:
        return "SetPlayerWantedLevel";

    case 134:
        return "ShowTextDraw";

    case 135:
        return "HideTextDraw";

    case 136:
        return "VehicleDestroyed";

    case 137:
        return "ServerJoin";

    case 138:
        return "ServerQuit";

    case 139:
        return "InitGame";

    case 140:
        return "MenuQuit";

    case 144:
        return "RemoveMapIcon";

    case 145:
        return "SetWeaponAmmo";

    case 146:
        return "SetGravity";

    case 147:
        return "SetVehicleHealth";

    case 148:
        return "AttachTrailerToVehicle";

    case 149:
        return "DetachTrailerFromVehicle";

    case 152:
        return "SetWeather";

    case 153:
        return "SetPlayerSkin";

    case 154:
        return "PlayerExitVehicle";

    case 155:
        return "UpdateScoresAndPings";

    case 156:
        return "SetInterior";

    case 157:
        return "SetCameraPos";

    case 158:
        return "SetCameraLookAt";

    case 159:
        return "SetVehiclePos";

    case 160:
        return "SetVehicleZAngle";

    case 161:
        return "SetVehicleParams";

    case 162:
        return "SetCameraBehind";

    case 163:
        return "WorldPlayerRemove";

    case 164:
        return "WorldVehicleAdd";

    case 165:
        return "WorldVehicleRemove";

    case 166:
        return "DeathBroadcast";

    case 167:
        return "DisableVehicleCollisions";

    case 168:
        return "CameraTarget";

    case 170:
        return "ToggleCameraTarget";

    case 171:
        return "ShowActor";

    case 172:
        return "HideActor";

    case 173:
        return "ApplyActorAnimation";

    case 174:
        return "ClearActorAnimation";

    case 175:
        return "SetActorFacingAngle";

    case 176:
        return "SetActorPos";

    case 177:
        return "GiveActorDamage";

    case 178:
        return "SetActorHealth";

    default:
        return nullptr;
    }
}

const char* packet_name(
    unsigned char id
)
{
    switch (id)
    {
    case 203:
        return "AimSync";

    case 200:
        return "VehicleSync";

    case 206:
        return "BulletSync";

    case 207:
        return "PlayerSync";

    case 209:
        return "UnoccupiedSync";

    case 210:
        return "TrailerSync";

    case 211:
        return "PassengerSync";

    case 212:
        return "SpectatorSync";

    default:
        return nullptr;
    }
}

} // namespace

namespace intercept::decoder {

DecodeResult decode_rpc(
    unsigned char id,
    const unsigned char* data,
    std::size_t size
)
{
    DecodeResult result{};

    const char* name = rpc_name(id);

    if (name)
    {
        result.name = name;

        std::ostringstream ss;

        ss
            << "id="
            << static_cast<unsigned int>(id)
            << " bytes="
            << size;

        result.details = ss.str();

        return result;
    }

    result.name = "UNKNOWN_RPC";

    std::ostringstream ss;

    ss
        << "id="
        << static_cast<unsigned int>(id)
        << " bytes="
        << size;

    result.details = ss.str();

    return result;
}

DecodeResult decode_packet(
    const unsigned char* data,
    std::size_t size
)
{
    DecodeResult result{};

    if (!data || size == 0)
    {
        result.name = "EMPTY_PACKET";
        result.details = "bytes=0";

        return result;
    }

    const unsigned char id = data[0];

    const char* name = packet_name(id);

    if (name)
    {
        result.name = name;

        std::ostringstream ss;

        ss
            << "id="
            << static_cast<unsigned int>(id)
            << " bytes="
            << size;

        result.details = ss.str();

        return result;
    }

    result.name = "UNKNOWN_PACKET";

    std::ostringstream ss;

    ss
        << "id="
        << static_cast<unsigned int>(id)
        << " bytes="
        << size;

    result.details = ss.str();

    return result;
}

} // namespace intercept::decoder
