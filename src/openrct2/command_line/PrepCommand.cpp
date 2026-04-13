/*****************************************************************************
 * Copyright (c) 2014-2021 OpenRCT2 developers, Cory Sanin
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include "../Context.h"
#include "../Game.h"
#include "../actions/GameActionRunner.h"
#include "../actions/ResultWithMessage.h"
#include "../EditorObjectSelectionSession.h"
#include "../FileClassifier.h"
#include "../GameState.h"
#include "../OpenRCT2.h"
#include "../ParkImporter.h"
#include "../actions/cheats/CheatSetAction.h"
#include "../actions/park/ParkSetDateAction.h"
#include "../actions/park/ParkSetParameterAction.h"
#include "../actions/general/ScenarioSetSettingAction.h"
#include "../core/Console.hpp"
#include "../core/Path.hpp"
#include "../entity/Staff.h"
#include "../object/ObjectManager.h"
#include "../object/ObjectRepository.h"
#include "../ride/RideManager.hpp"
#include "../scenario/Scenario.h"
#include "../world/Map.h"
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

    auto clearGrass = GameActions::CheatSetAction(CheatType::setGrassLength, GRASS_LENGTH_CLEAR_0);
    GameActions::Execute(&clearGrass, gameState);
    auto waterPlants = GameActions::CheatSetAction(CheatType::waterPlants);
    GameActions::Execute(&waterPlants, gameState);
    auto removeLitter = GameActions::CheatSetAction(CheatType::removeLitter);
    GameActions::Execute(&removeLitter, gameState);
    auto removeGuests = GameActions::CheatSetAction(CheatType::removeAllGuests);
    GameActions::Execute(&removeGuests, gameState);
    auto removeDucks = GameActions::CheatSetAction(CheatType::removeDucks);
    GameActions::Execute(&removeDucks, gameState);
    auto clearLoad = GameActions::CheatSetAction(CheatType::clearLoan);
    GameActions::Execute(&clearLoad, gameState);
    auto resetCrash = GameActions::CheatSetAction(CheatType::resetCrashStatus);
    GameActions::Execute(&resetCrash, gameState);
    auto fixRides = GameActions::CheatSetAction(CheatType::fixRides);
    GameActions::Execute(&fixRides, gameState);
    auto fixVandal = GameActions::CheatSetAction(CheatType::fixVandalism);
    GameActions::Execute(&fixVandal, gameState);
    auto renewRides = GameActions::CheatSetAction(CheatType::renewRides);
    GameActions::Execute(&renewRides, gameState);
    auto haveFun = GameActions::CheatSetAction(CheatType::haveFun, 1);
    GameActions::Execute(&haveFun, gameState);
    auto clearanceChecks = GameActions::CheatSetAction(CheatType::disableClearanceChecks, 0);
    GameActions::Execute(&clearanceChecks, gameState);
    auto supportLimits = GameActions::CheatSetAction(CheatType::disableSupportLimits, 0);
    GameActions::Execute(&supportLimits, gameState);
    auto sandboxMode = GameActions::CheatSetAction(CheatType::sandboxMode, 0);
    GameActions::Execute(&sandboxMode, gameState);
    auto operatingModes = GameActions::CheatSetAction(CheatType::showAllOperatingModes, 0);
    GameActions::Execute(&operatingModes, gameState);
    auto otherTrackVehicles = GameActions::CheatSetAction(CheatType::showVehiclesFromOtherTrackTypes, 0);
    GameActions::Execute(&otherTrackVehicles, gameState);
    auto trainLengthLimit = GameActions::CheatSetAction(CheatType::disableTrainLengthLimit, 0);
    GameActions::Execute(&trainLengthLimit, gameState);
    auto allTrackChainlift = GameActions::CheatSetAction(CheatType::enableChainLiftOnAllTrack, 0);
    GameActions::Execute(&allTrackChainlift, gameState);
    auto fastLiftHill = GameActions::CheatSetAction(CheatType::fastLiftHill, 0);
    GameActions::Execute(&fastLiftHill, gameState);
    auto brakeFailures = GameActions::CheatSetAction(CheatType::disableBrakesFailure, 0);
    GameActions::Execute(&brakeFailures, gameState);
    auto breakdowns = GameActions::CheatSetAction(CheatType::disableAllBreakdowns, 0);
    GameActions::Execute(&breakdowns, gameState);
    auto pauseModeBuild = GameActions::CheatSetAction(CheatType::buildInPauseMode, 0);
    GameActions::Execute(&pauseModeBuild, gameState);
    auto rideIntensity = GameActions::CheatSetAction(CheatType::ignoreRideIntensity, 0);
    GameActions::Execute(&rideIntensity, gameState);
    auto vandalismToggle = GameActions::CheatSetAction(CheatType::disableVandalism, 0);
    GameActions::Execute(&vandalismToggle, gameState);
    auto litterToggle = GameActions::CheatSetAction(CheatType::disableLittering, 0);
    GameActions::Execute(&litterToggle, gameState);
    auto plantAgeToggle = GameActions::CheatSetAction(CheatType::disablePlantAging, 0);
    GameActions::Execute(&plantAgeToggle, gameState);
    auto destructible = GameActions::CheatSetAction(CheatType::makeDestructible, 0);
    GameActions::Execute(&destructible, gameState);
    auto marketing = GameActions::CheatSetAction(CheatType::neverendingMarketing, 0);
    GameActions::Execute(&marketing, gameState);
    auto rideTypeChanges = GameActions::CheatSetAction(CheatType::allowArbitraryRideTypeChanges, 0);
    GameActions::Execute(&rideTypeChanges, gameState);
    auto rideValueAging = GameActions::CheatSetAction(CheatType::disableRideValueAging, 0);
    GameActions::Execute(&rideValueAging, gameState);
    auto researchStatus = GameActions::CheatSetAction(CheatType::ignoreResearchStatus, 0);
    GameActions::Execute(&researchStatus, gameState);
    auto invalidHeights = GameActions::CheatSetAction(CheatType::allowTrackPlaceInvalidHeights, 0);
    GameActions::Execute(&invalidHeights, gameState);

    auto setDate = GameActions::ParkSetDateAction(0, 0, 0);
    GameActions::Execute(&setDate, gameState);
    auto openPark = GameActions::ParkSetParameterAction(GameActions::ParkParameter::Open);
    GameActions::Execute(&openPark, gameState);

    gGamePaused = 0;

    gameState.newsItems.Clear();

    if (prepSandbox)
    {
        const ObjectRepositoryItem* items = ObjectRepositoryGetItems();
        int32_t numObjects = static_cast<int32_t>(ObjectRepositoryGetItemsCount());
        EditorInputFlags inputFlags = { EditorInputFlag::unk1, EditorInputFlag::selectObjectsInSceneryGroup };
        auto noMoney = GameActions::CheatSetAction(CheatType::noMoney, 1);
        GameActions::Execute(&noMoney, gameState);

        for (auto& rideRef : RideManager(gameState))
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
        auto yesMoney = GameActions::CheatSetAction(CheatType::noMoney, 0);
        GameActions::Execute(&yesMoney, gameState);
        auto parkChargeMethod = GameActions::ScenarioSetSettingAction(GameActions::ScenarioSetSetting::ParkChargeMethod, 0);
        GameActions::Execute(&parkChargeMethod, gameState);
        auto initialLoan = GameActions::ScenarioSetSettingAction(GameActions::ScenarioSetSetting::InitialLoan, 0);
        GameActions::Execute(&initialLoan, gameState);
        auto maxLoanSize = GameActions::ScenarioSetSettingAction(GameActions::ScenarioSetSetting::MaximumLoanSize, 0);
        GameActions::Execute(&maxLoanSize, gameState);
        auto annualInterest = GameActions::ScenarioSetSettingAction(GameActions::ScenarioSetSetting::AnnualInterestRate, 0);
        GameActions::Execute(&annualInterest, gameState);
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
        windowMgr->CloseByClass(WindowClass::mainWindow);

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
    for (auto& rideRef : RideManager(gameState))
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
        if (peep->AssignedStaffType == StaffType::handyman)
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
