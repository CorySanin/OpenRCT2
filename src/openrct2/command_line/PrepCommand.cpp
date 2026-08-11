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
#include "../actions/cheats/CheatSetAction.h"
#include "../actions/ResultWithMessage.h"
#include "../scenes/editor/EditorController.h"
#include "../FileClassifier.h"
#include "../GameState.h"
#include "../OpenRCT2.h"
#include "../ParkImporter.h"
#include "../park/ParkFile.h"
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
#include "../world/Location.hpp"

#include <memory>

using namespace OpenRCT2;

static void UpdateTrackElementsRideType();
static bool DetectProblems(GameState_t& gameState);

OpenRCT2::CommandLine::ExitCode CommandLine::HandleCommandPrep(CommandLineArgEnumerator* enumerator)
{
    ExitCode result = CommandLine::HandleCommandDefault();
    if (result != ExitCode::launch)
    {
        return result;
    }

    // Get the prep type
    const utf8* rawPrepType;
    if (!enumerator->TryPopString(&rawPrepType))
    {
        Console::Error::WriteLine("Expected a prep type");
        return ExitCode::fail;
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
            return ExitCode::fail;
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
                return ExitCode::fail;
            }
        }
    }
    else
    {
        Console::Error::WriteLine("Invalid prep type.");
        return ExitCode::fail;
    }

    // Get the source path
    const utf8* rawSourcePath;
    if (!enumerator->TryPopString(&rawSourcePath))
    {
        Console::Error::WriteLine("Expected a source path.");
        return ExitCode::fail;
    }

    const auto sourcePath = Path::GetAbsolute(rawSourcePath);
    auto sourceFileType = GetFileExtensionType(sourcePath);

    // Get the destination path
    const utf8* rawDestinationPath;
    if (!enumerator->TryPopString(&rawDestinationPath))
    {
        Console::Error::WriteLine("Expected a destination path.");
        return ExitCode::fail;
    }

    const auto destinationPath = Path::GetAbsolute(rawDestinationPath);
    auto destinationFileType = GetFileExtensionType(destinationPath);

    // Validate target type
    if (destinationFileType != FileExtension::PARK)
    {
        Console::Error::WriteLine("Only conversion to .PARK is supported.");
        return ExitCode::fail;
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
            return ExitCode::fail;
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
                return ExitCode::fail;
        }
    }
    catch (const std::exception& ex)
    {
        Console::Error::WriteLine(ex.what());
        return ExitCode::fail;
    }


    ScenarioBegin(gameState);

    gameState.lastEntranceStyle = objManager.GetLoadedObjectEntryIndex("rct2.station.plain");

    auto clearGrass = GameActions::CheatSetAction(CheatType::setGrassLength, GRASS_LENGTH_CLEAR_0);
    GameActions::ExecuteNested(&clearGrass, gameState);
    auto waterPlants = GameActions::CheatSetAction(CheatType::waterPlants);
    GameActions::ExecuteNested(&waterPlants, gameState);
    auto removeLitter = GameActions::CheatSetAction(CheatType::removeLitter);
    GameActions::ExecuteNested(&removeLitter, gameState);
    auto removeGuests = GameActions::CheatSetAction(CheatType::removeAllGuests);
    GameActions::ExecuteNested(&removeGuests, gameState);
    auto removeDucks = GameActions::CheatSetAction(CheatType::removeDucks);
    GameActions::ExecuteNested(&removeDucks, gameState);
    auto clearLoad = GameActions::CheatSetAction(CheatType::clearLoan);
    GameActions::ExecuteNested(&clearLoad, gameState);
    auto resetCrash = GameActions::CheatSetAction(CheatType::resetCrashStatus);
    GameActions::ExecuteNested(&resetCrash, gameState);
    auto fixRides = GameActions::CheatSetAction(CheatType::fixRides);
    GameActions::ExecuteNested(&fixRides, gameState);
    auto fixVandal = GameActions::CheatSetAction(CheatType::fixVandalism);
    GameActions::ExecuteNested(&fixVandal, gameState);
    auto renewRides = GameActions::CheatSetAction(CheatType::renewRides);
    GameActions::ExecuteNested(&renewRides, gameState);
    auto haveFun = GameActions::CheatSetAction(CheatType::haveFun);
    GameActions::ExecuteNested(&haveFun, gameState);
    auto clearanceChecks = GameActions::CheatSetAction(CheatType::disableClearanceChecks, 0);
    GameActions::ExecuteNested(&clearanceChecks, gameState);
    auto supportLimits = GameActions::CheatSetAction(CheatType::disableSupportLimits, 0);
    GameActions::ExecuteNested(&supportLimits, gameState);
    auto sandboxMode = GameActions::CheatSetAction(CheatType::sandboxMode, 0);
    GameActions::ExecuteNested(&sandboxMode, gameState);
    auto operatingModes = GameActions::CheatSetAction(CheatType::showAllOperatingModes, 0);
    GameActions::ExecuteNested(&operatingModes, gameState);
    auto otherTrackVehicles = GameActions::CheatSetAction(CheatType::showVehiclesFromOtherTrackTypes, 0);
    GameActions::ExecuteNested(&otherTrackVehicles, gameState);
    auto trainLengthLimit = GameActions::CheatSetAction(CheatType::disableTrainLengthLimit, 0);
    GameActions::ExecuteNested(&trainLengthLimit, gameState);
    auto allTrackChainlift = GameActions::CheatSetAction(CheatType::enableChainLiftOnAllTrack, 0);
    GameActions::ExecuteNested(&allTrackChainlift, gameState);
    auto fastLiftHill = GameActions::CheatSetAction(CheatType::fastLiftHill, 0);
    GameActions::ExecuteNested(&fastLiftHill, gameState);
    auto brakeFailures = GameActions::CheatSetAction(CheatType::disableBrakesFailure, 0);
    GameActions::ExecuteNested(&brakeFailures, gameState);
    auto breakdowns = GameActions::CheatSetAction(CheatType::disableAllBreakdowns, 0);
    GameActions::ExecuteNested(&breakdowns, gameState);
    auto pauseModeBuild = GameActions::CheatSetAction(CheatType::buildInPauseMode, 0);
    GameActions::ExecuteNested(&pauseModeBuild, gameState);
    auto rideIntensity = GameActions::CheatSetAction(CheatType::ignoreRideIntensity, 0);
    GameActions::ExecuteNested(&rideIntensity, gameState);
    auto vandalismToggle = GameActions::CheatSetAction(CheatType::disableVandalism, 0);
    GameActions::ExecuteNested(&vandalismToggle, gameState);
    auto litterToggle = GameActions::CheatSetAction(CheatType::disableLittering, 0);
    GameActions::ExecuteNested(&litterToggle, gameState);
    auto plantAgeToggle = GameActions::CheatSetAction(CheatType::disablePlantAging, 0);
    GameActions::ExecuteNested(&plantAgeToggle, gameState);
    auto destructible = GameActions::CheatSetAction(CheatType::makeDestructible, 0);
    GameActions::ExecuteNested(&destructible, gameState);
    auto marketing = GameActions::CheatSetAction(CheatType::neverendingMarketing, 0);
    GameActions::ExecuteNested(&marketing, gameState);
    auto rideTypeChanges = GameActions::CheatSetAction(CheatType::allowArbitraryRideTypeChanges, 0);
    GameActions::ExecuteNested(&rideTypeChanges, gameState);
    auto rideValueAging = GameActions::CheatSetAction(CheatType::disableRideValueAging, 0);
    GameActions::ExecuteNested(&rideValueAging, gameState);
    auto researchStatus = GameActions::CheatSetAction(CheatType::ignoreResearchStatus, 0);
    GameActions::ExecuteNested(&researchStatus, gameState);
    auto invalidHeights = GameActions::CheatSetAction(CheatType::allowTrackPlaceInvalidHeights, 0);
    GameActions::ExecuteNested(&invalidHeights, gameState);

    auto setDate = GameActions::ParkSetDateAction(0, 0, 0);
    GameActions::ExecuteNested(&setDate, gameState);
    auto openPark = GameActions::ParkSetParameterAction(GameActions::ParkParameter::open);
    GameActions::ExecuteNested(&openPark, gameState);

    gGamePaused = 0;

    gameState.newsItems.clear();

    if (prepSandbox)
    {
        const ObjectRepositoryItem* items = ObjectRepositoryGetItems();
        int32_t numObjects = static_cast<int32_t>(ObjectRepositoryGetItemsCount());
        Editor::InputFlags inputFlags = { Editor::InputFlag::unk1, Editor::InputFlag::selectObjectsInSceneryGroup };
        auto noMoney = GameActions::CheatSetAction(CheatType::noMoney, 1);
        GameActions::ExecuteNested(&noMoney, gameState);

        for (auto& rideRef : RideManager(gameState))
        {
            if (rideRef.type == RIDE_TYPE_CASH_MACHINE)
            {
                rideRef.type = RIDE_TYPE_FIRST_AID;
                rideRef.subtype = RideGetEntryIndex(RIDE_TYPE_FIRST_AID, kObjectEntryIndexNull);
            }
        }
        UpdateTrackElementsRideType();

        Editor::Sub6AB211();
        for (int32_t i = 0; i < numObjects; i++)
        {
            const ObjectRepositoryItem* item = &items[i];
            if (item->Name == "Cash Machine")
            {
                Editor::ObjectSelectionSelectObject(0, inputFlags, item);
            }
        }

        Editor::UnloadUnselectedObjects();
        Editor::ObjectFlagsClear();
    }
    if (prepEcon)
    {
        auto yesMoney = GameActions::CheatSetAction(CheatType::noMoney, 0);
        GameActions::ExecuteNested(&yesMoney, gameState);
        auto parkChargeMethod = GameActions::ScenarioSetSettingAction(GameActions::ScenarioSetSetting::parkChargeMethod, 0);
        GameActions::ExecuteNested(&parkChargeMethod, gameState);
        auto initialLoan = GameActions::ScenarioSetSettingAction(GameActions::ScenarioSetSetting::initialLoan, 0);
        GameActions::ExecuteNested(&initialLoan, gameState);
        auto maxLoanSize = GameActions::ScenarioSetSettingAction(GameActions::ScenarioSetSetting::maximumLoanSize, 0);
        GameActions::ExecuteNested(&maxLoanSize, gameState);
        auto annualInterest = GameActions::ScenarioSetSettingAction(GameActions::ScenarioSetSetting::annualInterestRate, 0);
        GameActions::ExecuteNested(&annualInterest, gameState);
        gameState.park.cash = econBudget;
        gameState.park.flags |= PARK_FLAGS_PARK_FREE_ENTRY;
    }

    if (!DetectProblems(gameState))
    {
        return ExitCode::fail;
    }

    try
    {
        auto exporter = std::make_unique<ParkFileExporter>();

        // HACK remove the main window so it saves the park with the
        //      correct initial view
        //      taken from ConvertCommand.cpp
        auto* windowMgr = Ui::GetWindowManager();
        windowMgr->CloseByClass(WindowClass::mainWindow);

        exporter->Export(gameState, destinationPath, static_cast<int16_t>(kParkFileSaveCompressionLevel));
    }
    catch (const std::exception& ex)
    {
        Console::Error::WriteLine(ex.what());
        return ExitCode::fail;
    }

    Console::WriteLine("Execution complete.");
    return ExitCode::ok;
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
                if (tileElement->getType() != TileElementType::track)
                    continue;

                auto* trackElement = tileElement->asTrack();
                const auto* ride = GetRide(trackElement->GetRideIndex());
                if (ride != nullptr)
                {
                    trackElement->SetRideType(ride->type);
                }

            } while (!(tileElement++)->isLastForTile());
        }
    }
}

static bool DetectProblems(GameState_t& gameState)
{

    if (gameState.cheats.sandboxMode)
    {
        Console::Error::WriteLine("sandbox mode is enabled!");
        return false;
    }

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
        if (peep->assignedStaffType == StaffType::handyman)
        {
            hmen++;
        }
    }

    if (hmen < static_cast<uint32_t>(gameState.mapSize.x * gameState.mapSize.y / 800))
    {
        Console::Error::WriteLine("PREP: Consider adding more handymen to the park.");
    }
    if (!food)
    {
        Console::Error::WriteLine("PREP: Consider adding a food stall to the park.");
    }
    if (!drink)
    {
        Console::Error::WriteLine("PREP: Consider adding a drink stall to the park.");
    }
    if (!restroom)
    {
        Console::Error::WriteLine("PREP: Consider adding a restroom stall to the park.");
    }
    if (!ride)
    {
        Console::Error::WriteLine("PREP: Consider adding a ride to the park.");
    }
    return true;
}
