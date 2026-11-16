#pragma once

#include "Pch.h"

namespace SkyPotion::Game {

    struct AlchemySelectResult {
        bool success{ false };
        std::string message;
        std::vector<std::string> selectedIngredients;
        std::vector<std::string> missingIngredients;
        bool crafted{ false };

        nlohmann::json ToJson() const {
            return {
                { "success", success },
                { "message", message },
                { "selectedIngredients", selectedIngredients },
                { "missingIngredients", missingIngredients },
                { "crafted", crafted }
            };
        }
    };

    class AlchemyController {
    public:
        // Checks if the Alchemy workbench menu is currently open
        static bool IsAlchemyMenuOpen();

        // Retrieves the active AlchemyMenu pointer, or nullptr if not open
        static RE::CraftingSubMenus::AlchemyMenu* GetAlchemyMenu();

        // Clears any currently selected ingredients from the alchemy slots
        static bool ClearSelectedIngredients();

        // Selects ingredients by string FormIDs (hex "0x..." or decimal)
        static AlchemySelectResult SelectIngredients(
            const std::vector<std::string>& formIdStrings,
            bool autoCraft = false
        );

        // Selects ingredients by numeric FormIDs
        static AlchemySelectResult SelectIngredients(
            const std::vector<RE::FormID>& formIds,
            bool autoCraft = false
        );
    };

    class MenuOpenCloseHandler : public RE::BSTEventSink<RE::MenuOpenCloseEvent> {
    public:
        static MenuOpenCloseHandler* GetSingleton();
        void Register();
        RE::BSEventNotifyControl ProcessEvent(
            const RE::MenuOpenCloseEvent* a_event,
            RE::BSTEventSource<RE::MenuOpenCloseEvent>* a_eventSource) override;
    };

}
