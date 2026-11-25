#pragma once

#include "Pch.h"

namespace SkyPotion::Game {

    struct AlchemySelectResult {
        bool success{ false };
        std::string message;
        std::vector<std::string> selectedIngredients;
        std::vector<std::string> missingIngredients;
        bool crafted{ false };
        int craftedCount{ 0 };
    };

    class AlchemyController {
    public:
        // Checks if the Alchemy workbench menu is currently open
        static bool IsAlchemyMenuOpen();

        // Checks if the Alchemy menu AND its Flash item list are fully populated and visible
        static bool IsAlchemyItemListReady();

        // Retrieves the active AlchemyMenu pointer, or nullptr if not open
        static RE::CraftingSubMenus::AlchemyMenu* GetAlchemyMenu();

        // Clears any currently selected ingredients from the alchemy slots
        static bool ClearSelectedIngredients();

        // Gets the FormIDs of all currently selected ingredients in the alchemy slots
        static std::vector<RE::FormID> GetSelectedIngredientFormIDs();

        // Crafts potions with the currently selected ingredients
        static AlchemySelectResult CraftPotions(int count = 1);

        // Stops any currently running bulk crafting session
        static bool StopCrafting();

        // Checks if bulk crafting is currently running
        static bool IsCrafting();

        // Dismisses any open or queued message box dialogs (e.g. effect discovery prompts)
        static void DismissOpenMessageBoxes();

        // Selects ingredients by string FormIDs (hex "0x..." or decimal)
        static AlchemySelectResult SelectIngredients(
            const std::vector<std::string>& formIdStrings,
            bool autoCraft = false,
            int craftCount = 1
        );

        // Selects ingredients by numeric FormIDs
        static AlchemySelectResult SelectIngredients(
            const std::vector<RE::FormID>& formIds,
            bool autoCraft = false,
            int craftCount = 1
        );
    };

    class MenuOpenCloseHandler : public RE::BSTEventSink<RE::MenuOpenCloseEvent> {
    public:
        static MenuOpenCloseHandler* GetSingleton();
        void Register();

        static void StartMonitoring();
        static void StopMonitoring();
        static bool IsMonitoring();

        RE::BSEventNotifyControl ProcessEvent(
            const RE::MenuOpenCloseEvent* a_event,
            RE::BSTEventSource<RE::MenuOpenCloseEvent>* a_eventSource) override;
    };

}
