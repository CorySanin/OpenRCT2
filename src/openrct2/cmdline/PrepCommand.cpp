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
#include "../OpenRCT2.h"
#include "../ParkFile.h"
#include "../ParkImporter.h"
#include "../actions/ParkSetDateAction.h"
#include "../actions/ParkSetParameterAction.h"
#include "../actions/PauseToggleAction.h"
#include "../core/Console.hpp"
#include "../core/Path.hpp"
#include "../object/ObjectManager.h"
#include "../object/ObjectRepository.h"
#include "../scenario/Scenario.h"
#include "../world/Park.h"
#include "../world/Surface.h"
#include "../entity/Staff.h"
#include "../management/NewsItem.h"
#include "CommandLine.hpp"

#include <memory>

static void UpdateTrackElementsRideType();
static void DetectProblems();

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
    if (String::Equals(rawPrepType, "sandbox", true))
    {
        prepSandbox = true;
    }
    else if (String::Equals(rawPrepType, "economy", true))
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

    utf8 sourcePath[MAX_PATH];
    Path::GetAbsolute(sourcePath, sizeof(sourcePath), rawSourcePath);
    uint32_t sourceFileType = get_file_extension_type(sourcePath);

    // Get the destination path
    const utf8* rawDestinationPath;
    if (!enumerator->TryPopString(&rawDestinationPath))
    {
        Console::Error::WriteLine("Expected a destination path.");
        return EXITCODE_FAIL;
    }

    utf8 destinationPath[MAX_PATH];
    Path::GetAbsolute(destinationPath, sizeof(sourcePath), rawDestinationPath);
    uint32_t destinationFileType = get_file_extension_type(destinationPath);

    // Validate target type
    if (destinationFileType != FILE_EXTENSION_PARK)
    {
        Console::Error::WriteLine("Only conversion to .PARK is supported.");
        return EXITCODE_FAIL;
    }

    // Validate the source type
    switch (sourceFileType)
    {
        case FILE_EXTENSION_SC4:
        case FILE_EXTENSION_SV4:
        case FILE_EXTENSION_SC6:
        case FILE_EXTENSION_SV6:
        case FILE_EXTENSION_PARK:
            break;
        default:
            Console::Error::WriteLine("Only conversion from .SC4, .SV4, .SC6, .SV6, or .PARK is supported.");
            return EXITCODE_FAIL;
    }

    // Perform preparation
    gOpenRCT2Headless = true;
    auto context = OpenRCT2::CreateContext();
    context->Initialise();

    auto& objManager = context->GetObjectManager();

    try
    {
        switch (sourceFileType)
        {
            case FILE_EXTENSION_SC4:
            case FILE_EXTENSION_SV4:
            case FILE_EXTENSION_SC6:
            case FILE_EXTENSION_SV6:
            {
                auto importer = ParkImporter::Create(sourcePath);
                auto loadResult = importer->Load(sourcePath);

                objManager.LoadObjects(loadResult.RequiredObjects);

                importer->Import();
            }
            break;
            case FILE_EXTENSION_PARK:
            {
                std::unique_ptr<IParkImporter> importer = ParkImporter::CreateParkFile(context->GetObjectRepository());
                auto loadResult = importer->Load(sourcePath);

                objManager.LoadObjects(loadResult.RequiredObjects);

                importer->Import();
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

    if (sourceFileType == FILE_EXTENSION_SC4 || sourceFileType == FILE_EXTENSION_SC6)
    {
        // We are converting a scenario, so reset the park
        scenario_begin();
    }

    CheatsSet(CheatType::SetGrassLength, GRASS_LENGTH_CLEAR_0);
    CheatsSet(CheatType::WaterPlants);
    CheatsSet(CheatType::RemoveLitter);
    CheatsSet(CheatType::RemoveAllGuests);
    CheatsSet(CheatType::RemoveDucks);

    auto setDateAction = ParkSetDateAction(1, 1, 1);
    GameActions::Execute(&setDateAction);

    auto parkSetParameter = ParkSetParameterAction(ParkParameter::Open);
    GameActions::Execute(&parkSetParameter);

    if (gGamePaused & GAME_PAUSED_NORMAL)
    {
        auto pauseToggleAction = PauseToggleAction();
        GameActions::Execute(&pauseToggleAction);
    }

    gNewsItems.Clear();

    if (prepSandbox)
    {
        CheatsSet(CheatType::NoMoney, 1);
        for (auto& rideRef : GetRideManager())
        {
            if (rideRef.type == RIDE_TYPE_CASH_MACHINE)
            {
                rideRef.type = RIDE_TYPE_FIRST_AID;
                rideRef.subtype = 58;
            }
        }
        UpdateTrackElementsRideType();

        const ObjectRepositoryItem* items = object_repository_get_items();
        int32_t numObjects = static_cast<int32_t>(object_repository_get_items_count());
        int32_t flags = INPUT_FLAG_EDITOR_OBJECT_1 | INPUT_FLAG_EDITOR_OBJECT_SELECT_OBJECTS_IN_SCENERY_GROUP;

        // if (prepEcon)
        //     flags |= INPUT_FLAG_EDITOR_OBJECT_SELECT; // enable the ATM
        sub_6AB211();
        for (int32_t i = 0; i < numObjects; i++)
        {
            const ObjectRepositoryItem* item = &items[i];
            if (item->Id == 508 || item->Id == 507)
            {
                window_editor_object_selection_select_object(0, flags, item);
            }
        }

        unload_unselected_objects();
        editor_object_flags_free();
    }
    if (prepEcon)
    {
        CheatsSet(CheatType::NoMoney, 0);
        CheatsSet(CheatType::SetMoney, econBudget);
    }

    DetectProblems();

    try
    {
        auto exporter = std::make_unique<ParkFileExporter>();

        // HACK remove the main window so it saves the park with the
        //      correct initial view
        //      taken from ConvertCommand.cpp
        window_close_by_class(WC_MAIN_WINDOW);

        exporter->Export(destinationPath);
    }
    catch (const std::exception& ex)
    {
        Console::Error::WriteLine(ex.what());
        return EXITCODE_FAIL;
    }

    Console::WriteLine("Conversion successful!");
    return EXITCODE_OK;
}

static void UpdateTrackElementsRideType()
{
    for (int32_t x = 0; x < MAXIMUM_MAP_SIZE_TECHNICAL; x++)
    {
        for (int32_t y = 0; y < MAXIMUM_MAP_SIZE_TECHNICAL; y++)
        {
            TileElement* tileElement = map_get_first_element_at(TileCoordsXY{ x, y });
            if (tileElement == nullptr)
                continue;
            do
            {
                if (tileElement->GetType() != TILE_ELEMENT_TYPE_TRACK)
                    continue;

                auto* trackElement = tileElement->AsTrack();
                const auto* ride = get_ride(trackElement->GetRideIndex());
                if (ride != nullptr)
                {
                    trackElement->SetRideType(ride->type);
                }

            } while (!(tileElement++)->IsLastForTile());
        }
    }
}

static void DetectProblems()
{
    bool food = false;
    bool drink = false;
    bool restroom = false;
    bool ride = false;
    for (auto& rideRef : GetRideManager())
    {
        if (rideRef.mode == RideMode::ShopStall)
        {
            food = food || (rideRef.type == RIDE_TYPE_FOOD_STALL && rideRef.status == RideStatus::Open);
            drink = drink || (rideRef.type == RIDE_TYPE_DRINK_STALL && rideRef.status == RideStatus::Open);
            restroom = restroom || (rideRef.type == RIDE_TYPE_TOILETS && rideRef.status == RideStatus::Open);
        }
        else
        {
            ride = ride || rideRef.status == RideStatus::Open;
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

    if (hmen < gParkSize / 800)
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
