/*****************************************************************************
 * Copyright (c) 2014-2021 OpenRCT2 developers, Cory Sanin
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include "../Context.h"
#include "../EditorObjectSelectionSession.h"
#include "../FileClassifier.h"
#include "../GameState.h"
#include "../OpenRCT2.h"
#include "../ParkImporter.h"
#include "../actions/CheatSetAction.h"
#include "../actions/ParkSetDateAction.h"
#include "../actions/ParkSetParameterAction.h"
#include "../actions/ScenarioSetSettingAction.h"
#include "../core/Console.hpp"
#include "../core/Path.hpp"
#include "../entity/Staff.h"
#include "../object/ObjectManager.h"
#include "../object/ObjectRepository.h"
#include "../ride/RideManager.hpp"
#include "../scenario/Scenario.h"
#include "../world/tile_element/SurfaceElement.h"
#include "../world/tile_element/TrackElement.h"
#include "../ui/WindowManager.h"
#include "CommandLine.hpp"
#include "../entity/EntityList.h"

#include <memory>

using namespace OpenRCT2;

static void UpdateTrackElementsRideType();
static void DetectProblems(GameState_t& gameState);

exitcode_t CommandLine::HandleCommandPrep(CommandLineArgEnumerator* enumerator)
{
    exitcode_t result = CommandLine::HandleCommandDefault();
    if (result != EXITCODE_CONTINUE)
    {
        return result;
    }

    // Get the prep type
    const utf8* rawPrepType;
    if (!enumerator->TryPopString(&rawPrepType))
    {
        Console::Error::WriteLine("Expected a prep type");
        return EXITCODE_FAIL;
    }

    const utf8* rawArg;
    bool prepSandbox = false;
    bool prepEcon = false;
    uint32_t econBudget = 0;
    if (String::equals(rawPrepType, "sandbox", true))
    {
        prepSandbox = true;
    }
    else if (String::equals(rawPrepType, "economy", true))
    {
        prepEcon = true;
        if (!enumerator->TryPopString(&rawArg))
        {
            Console::Error::WriteLine("Expected a starting fund value.");
            return EXITCODE_FAIL;
        }
        else
        {
            try
            {
                econBudget = std::stoi(rawArg);
            }
            catch (...)
            {
                Console::Error::WriteLine("Expected anumeric value for the starting fund.");
                return EXITCODE_FAIL;
            }
        }
    }
    else
    {
        Console::Error::WriteLine("Invalid prep type.");
        return EXITCODE_FAIL;
    }

    // Get the source path
    const utf8* rawSourcePath;
    if (!enumerator->TryPopString(&rawSourcePath))
    {
        Console::Error::WriteLine("Expected a source path.");
        return EXITCODE_FAIL;
    }

    const auto sourcePath = Path::GetAbsolute(rawSourcePath);
    auto sourceFileType = GetFileExtensionType(sourcePath);

    // Get the destination path
    const utf8* rawDestinationPath;
    if (!enumerator->TryPopString(&rawDestinationPath))
    {
        Console::Error::WriteLine("Expected a destination path.");
        return EXITCODE_FAIL;
    }

    const auto destinationPath = Path::GetAbsolute(rawDestinationPath);
    auto destinationFileType = GetFileExtensionType(destinationPath);

    // Validate target type
    if (destinationFileType != FileExtension::PARK)
    {
        Console::Error::WriteLine("Only conversion to .PARK is supported.");
        return EXITCODE_FAIL;
    }

    // Validate the source type
    switch (sourceFileType)
    {
        case FileExtension::SC4:
        case FileExtension::SV4:
        case FileExtension::SC6:
        case FileExtension::SV6:
        case FileExtension::PARK:
            break;
        default:
            Console::Error::WriteLine("Only conversion from .SC4, .SV4, .SC6, .SV6, or .PARK is supported.");
            return EXITCODE_FAIL;
    }

    // Perform preparation
    gOpenRCT2Headless = true;
    auto context = CreateContext();
    context->Initialise();

    auto& objManager = context->GetObjectManager();
    auto& gameState = getGameState();

    try
    {
        switch (sourceFileType)
        {
            case FileExtension::SC4:
            case FileExtension::SV4:
            case FileExtension::SC6:
            case FileExtension::SV6:
            {
                auto importer = ParkImporter::Create(sourcePath);
                auto loadResult = importer->Load(sourcePath.c_str(), false);

                objManager.LoadObjects(loadResult.RequiredObjects);

                importer->Import(gameState);
            }
            break;
            case FileExtension::PARK:
            {
                std::unique_ptr<IParkImporter> importer = ParkImporter::CreateParkFile(context->GetObjectRepository());
                auto loadResult = importer->Load(sourcePath.c_str(), false);

                objManager.LoadObjects(loadResult.RequiredObjects);

                importer->Import(gameState);
            }
            break;
            default:
                Console::Error::WriteLine("Only conversion from .SC4, .SV4, .SC6, .SV6, or .PARK is supported.");
                return EXITCODE_FAIL;
        }
    }
    catch (const std::exception& ex)
    {
        Console::Error::WriteLine(ex.what());
        return EXITCODE_FAIL;
    }


    ScenarioBegin(gameState);

    gameState.lastEntranceStyle = objManager.GetLoadedObjectEntryIndex("rct2.station.plain");

    GameActions::CheatSetAction(CheatType::SetGrassLength, GRASS_LENGTH_CLEAR_0).Execute();
    GameActions::CheatSetAction(CheatType::WaterPlants).Execute();
    GameActions::CheatSetAction(CheatType::RemoveLitter).Execute();
    GameActions::CheatSetAction(CheatType::RemoveAllGuests).Execute();
    GameActions::CheatSetAction(CheatType::RemoveDucks).Execute();
    GameActions::CheatSetAction(CheatType::ClearLoan).Execute();
    GameActions::CheatSetAction(CheatType::ResetCrashStatus).Execute();
    GameActions::CheatSetAction(CheatType::FixRides).Execute();
    GameActions::CheatSetAction(CheatType::FixVandalism).Execute();
    GameActions::CheatSetAction(CheatType::RenewRides).Execute();
    GameActions::CheatSetAction(CheatType::HaveFun, 1).Execute();
    GameActions::CheatSetAction(CheatType::DisableClearanceChecks, 0).Execute();
    GameActions::CheatSetAction(CheatType::DisableSupportLimits, 0).Execute();
    GameActions::CheatSetAction(CheatType::DisableSupportLimits, 0).Execute();
    GameActions::CheatSetAction(CheatType::SandboxMode, 0).Execute();
    GameActions::CheatSetAction(CheatType::ShowAllOperatingModes, 0).Execute();
    GameActions::CheatSetAction(CheatType::ShowVehiclesFromOtherTrackTypes, 0).Execute();
    GameActions::CheatSetAction(CheatType::DisableTrainLengthLimit, 0).Execute();
    GameActions::CheatSetAction(CheatType::EnableChainLiftOnAllTrack, 0).Execute();
    GameActions::CheatSetAction(CheatType::FastLiftHill, 0).Execute();
    GameActions::CheatSetAction(CheatType::DisableBrakesFailure, 0).Execute();
    GameActions::CheatSetAction(CheatType::DisableAllBreakdowns, 0).Execute();
    GameActions::CheatSetAction(CheatType::BuildInPauseMode, 0).Execute();
    GameActions::CheatSetAction(CheatType::IgnoreRideIntensity, 0).Execute();
    GameActions::CheatSetAction(CheatType::DisableVandalism, 0).Execute();
    GameActions::CheatSetAction(CheatType::DisableLittering, 0).Execute();
    GameActions::CheatSetAction(CheatType::DisablePlantAging, 0).Execute();
    GameActions::CheatSetAction(CheatType::MakeDestructible, 0).Execute();
    GameActions::CheatSetAction(CheatType::NeverendingMarketing, 0).Execute();
    GameActions::CheatSetAction(CheatType::AllowArbitraryRideTypeChanges, 0).Execute();
    GameActions::CheatSetAction(CheatType::DisableRideValueAging, 0).Execute();
    GameActions::CheatSetAction(CheatType::IgnoreResearchStatus, 0).Execute();
    GameActions::CheatSetAction(CheatType::AllowTrackPlaceInvalidHeights, 0).Execute();

    GameActions::ParkSetDateAction(0, 0, 0).Execute();

    GameActions::ParkSetParameterAction(GameActions::ParkParameter::Open).Execute();

    gGamePaused = 0;

    gameState.newsItems.Clear();

    if (prepSandbox)
    {
        const ObjectRepositoryItem* items = ObjectRepositoryGetItems();
        int32_t numObjects = static_cast<int32_t>(ObjectRepositoryGetItemsCount());
        EditorInputFlags inputFlags = { EditorInputFlag::unk1, EditorInputFlag::selectObjectsInSceneryGroup };
        GameActions::CheatSetAction(CheatType::NoMoney, 1).Execute();

        for (auto& rideRef : GetRideManager())
        {
            if (rideRef.type == RIDE_TYPE_CASH_MACHINE)
            {
                rideRef.type = RIDE_TYPE_FIRST_AID;
                rideRef.subtype = RideGetEntryIndex(RIDE_TYPE_FIRST_AID, kObjectEntryIndexNull);
            }
        }
        UpdateTrackElementsRideType();

        Sub6AB211();
        for (int32_t i = 0; i < numObjects; i++)
        {
            const ObjectRepositoryItem* item = &items[i];
            if (item->Name == "Cash Machine")
            {
                WindowEditorObjectSelectionSelectObject(0, inputFlags, item);
            }
        }

        UnloadUnselectedObjects();
        EditorObjectFlagsClear();
    }
    if (prepEcon)
    {
        GameActions::CheatSetAction(CheatType::NoMoney, 0).Execute();
        GameActions::ScenarioSetSettingAction(GameActions::ScenarioSetSetting::ParkChargeMethod, 0).Execute();
        GameActions::ScenarioSetSettingAction(GameActions::ScenarioSetSetting::InitialLoan, 0).Execute();
        GameActions::ScenarioSetSettingAction(GameActions::ScenarioSetSetting::MaximumLoanSize, 0).Execute();
        GameActions::ScenarioSetSettingAction(GameActions::ScenarioSetSetting::AnnualInterestRate, 0).Execute();
        gameState.park.cash = econBudget;
        gameState.park.flags |= PARK_FLAGS_PARK_FREE_ENTRY;
    }

    DetectProblems(gameState);

    try
    {
        // HACK remove the main window so it saves the park with the
        //      correct initial view
        //      taken from ConvertCommand.cpp
        auto* windowMgr = Ui::GetWindowManager();
        windowMgr->CloseByClass(WindowClass::MainWindow);

        SaveGameWithName(destinationPath);
    }
    catch (const std::exception& ex)
    {
        Console::Error::WriteLine(ex.what());
        return EXITCODE_FAIL;
    }

    Console::WriteLine("Execution complete.");
    return EXITCODE_OK;
}

static void UpdateTrackElementsRideType()
{
    auto& gameState = getGameState();
    for (int32_t y = 0; y < gameState.mapSize.y; y++)
    {
        for (int32_t x = 0; x < gameState.mapSize.x; x++)
        {
            TileElement* tileElement = MapGetFirstElementAt(TileCoordsXY{ x, y });
            if (tileElement == nullptr)
                continue;
            do
            {
                if (tileElement->GetType() != TileElementType::Track)
                    continue;

                auto* trackElement = tileElement->AsTrack();
                const auto* ride = GetRide(trackElement->GetRideIndex());
                if (ride != nullptr)
                {
                    trackElement->SetRideType(ride->type);
                }

            } while (!(tileElement++)->IsLastForTile());
        }
    }
}

static void DetectProblems(GameState_t& gameState)
{
    bool food = false;
    bool drink = false;
    bool restroom = false;
    bool ride = false;
    for (auto& rideRef : GetRideManager())
    {
        if (rideRef.mode == RideMode::shopStall)
        {
            food = food || (rideRef.type == RIDE_TYPE_FOOD_STALL && rideRef.status == RideStatus::open);
            drink = drink || (rideRef.type == RIDE_TYPE_DRINK_STALL && rideRef.status == RideStatus::open);
            restroom = restroom || (rideRef.type == RIDE_TYPE_TOILETS && rideRef.status == RideStatus::open);
        }
        else
        {
            ride = ride || rideRef.status == RideStatus::open;
        }
    }

    uint32_t hmen = 0;
    for (auto peep : EntityList<Staff>())
    {
        if (peep->AssignedStaffType == StaffType::Handyman)
        {
            hmen++;
        }
    }

    if (hmen < static_cast<uint32_t>(gameState.mapSize.x * gameState.mapSize.y / 800))
    {
        Console::Error::WriteLine("Consider adding more handymen to the park.");
    }
    if (!food)
    {
        Console::Error::WriteLine("Consider adding a food stall to the park.");
    }
    if (!drink)
    {
        Console::Error::WriteLine("Consider adding a drink stall to the park.");
    }
    if (!restroom)
    {
        Console::Error::WriteLine("Consider adding a restroom stall to the park.");
    }
    if (!ride)
    {
        Console::Error::WriteLine("Consider adding a ride to the park.");
    }
}
