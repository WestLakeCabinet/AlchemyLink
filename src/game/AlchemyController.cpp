#include "game/AlchemyController.h"
#include "server/WebSocketServer.h"
#include <chrono>

namespace SkyPotion::Game {

    RE::CraftingSubMenus::AlchemyMenu* AlchemyController::GetAlchemyMenu() {
        auto* ui = RE::UI::GetSingleton();
        if (!ui || !ui->IsMenuOpen(RE::CraftingMenu::MENU_NAME)) {
            return nullptr;
        }

        auto craftingMenu = ui->GetMenu<RE::CraftingMenu>(RE::CraftingMenu::MENU_NAME);
        if (!craftingMenu) {
            return nullptr;
        }

        auto* subMenu = craftingMenu->GetCraftingSubMenu();
        if (!subMenu) {
            return nullptr;
        }

        return skyrim_cast<RE::CraftingSubMenus::AlchemyMenu*>(subMenu);
    }

    bool AlchemyController::IsAlchemyMenuOpen() {
        return GetAlchemyMenu() != nullptr;
    }

    RE::GFxValue GetTargetItemList(RE::CraftingSubMenus::AlchemyMenu* alchemyMenu, RE::CraftingMenu* craftingMenu) {
        if (alchemyMenu && alchemyMenu->itemList.IsObject()) {
            return alchemyMenu->itemList;
        }

        RE::GFxValue list;
        auto* movie = craftingMenu ? craftingMenu->uiMovie.get() : (alchemyMenu ? alchemyMenu->view.get() : nullptr);
        if (movie) {
            if (movie->GetVariable(&list, "_root.Menu.InventoryLists.ItemList") && list.IsObject()) return list;
            if (movie->GetVariable(&list, "_root.CraftingMenu.InventoryLists.itemList") && list.IsObject()) return list;
            if (movie->GetVariable(&list, "_root.Menu_mc.inventoryLists.itemList") && list.IsObject()) return list;
            if (movie->GetVariable(&list, "_root.CraftingMenu.inventoryLists.itemList") && list.IsObject()) return list;
        }
        return list;
    }

    void RefreshAlchemyUI(RE::CraftingSubMenus::AlchemyMenu* alchemyMenu) {
        if (!alchemyMenu) return;

        SKSE::log::info("RefreshAlchemyUI: view={}, craftingMenu.isObj={}, itemList.isObj={}",
            (void*)alchemyMenu->view.get(),
            alchemyMenu->craftingMenu.IsObject(),
            alchemyMenu->itemList.IsObject());

        if (alchemyMenu->craftingMenu.IsObject()) {
            alchemyMenu->craftingMenu.Invoke("UpdateCategories");
            alchemyMenu->craftingMenu.Invoke("UpdateItemList");
        }

        if (alchemyMenu->itemList.IsObject()) {
            alchemyMenu->itemList.Invoke("Update");
            alchemyMenu->itemList.Invoke("UpdateList");
            alchemyMenu->itemList.Invoke("InvalidateData");
        }

        if (alchemyMenu->view) {
            alchemyMenu->view->Invoke("_root.CraftingMenu.UpdateCategories", nullptr, nullptr, 0);
            alchemyMenu->view->Invoke("_root.CraftingMenu.UpdateItemList", nullptr, nullptr, 0);
            alchemyMenu->view->Invoke("_root.CraftingMenu.InventoryLists.itemList.UpdateList", nullptr, nullptr, 0);
            alchemyMenu->view->Invoke("_root.CraftingMenu.InventoryLists.itemList.InvalidateData", nullptr, nullptr, 0);
            alchemyMenu->view->Invoke("_root.Menu_mc.inventoryLists.itemList.UpdateList", nullptr, nullptr, 0);
            alchemyMenu->view->Invoke("_root.Menu_mc.inventoryLists.itemList.InvalidateData", nullptr, nullptr, 0);
        }
    }

    bool AlchemyController::ClearSelectedIngredients() {
        auto* alchemyMenu = GetAlchemyMenu();
        if (!alchemyMenu) {
            return false;
        }

        auto* ui = RE::UI::GetSingleton();
        auto craftingMenu = (ui && ui->IsMenuOpen(RE::CraftingMenu::MENU_NAME))
            ? ui->GetMenu<RE::CraftingMenu>(RE::CraftingMenu::MENU_NAME)
            : nullptr;

        RE::GFxValue targetList = GetTargetItemList(alchemyMenu, craftingMenu.get());
        SKSE::log::info("ClearSelectedIngredients: targetList.isObj={}, selectedIndexes.size={}",
            targetList.IsObject(), alchemyMenu->selectedIndexes.size());

        // 1. Untoggle any selected items in Flash UI
        if (targetList.IsObject()) {
            RE::GFxValue entryList;
            if (targetList.GetMember("entryList", &entryList) && entryList.IsArray()) {
                uint32_t count = entryList.GetArraySize();
                for (uint32_t j = 0; j < count; ++j) {
                    RE::GFxValue entryObj;
                    entryList.GetElement(j, &entryObj);
                    if (entryObj.IsObject()) {
                        RE::GFxValue isSel;
                        bool sel = false;
                        if (entryObj.GetMember("selected", &isSel) && isSel.IsBool()) {
                            sel = isSel.GetBool();
                        } else if (entryObj.GetMember("isSelected", &isSel) && isSel.IsBool()) {
                            sel = isSel.GetBool();
                        }

                        if (sel) {
                            SKSE::log::info("ClearSelectedIngredients: Untoggling Flash entry at index {}", j);
                            RE::GFxValue indexVal(static_cast<double>(j));
                            targetList.SetMember("selectedIndex", indexVal);

                            RE::GFxValue args[2];
                            args[0] = static_cast<double>(j);
                            args[1] = 0.0;
                            targetList.Invoke("onItemSelect", nullptr, args, 1);
                            targetList.Invoke("onItemPress", nullptr, args, 2);
                        }
                    }
                }
            }
            targetList.Invoke("UpdateList");
        }

        // 2. Also deselect in C++ engine
        auto* userEvents = RE::UserEvents::GetSingleton();
        if (userEvents) {
            int maxTries = 10;
            while (!alchemyMenu->selectedIndexes.empty() && maxTries-- > 0) {
                std::uint32_t prevSize = static_cast<std::uint32_t>(alchemyMenu->selectedIndexes.size());
                std::uint32_t firstIdx = alchemyMenu->selectedIndexes.front();

                alchemyMenu->currentIngredientIdx = firstIdx;
                bool ok = alchemyMenu->ProcessUserEvent(&userEvents->accept);
                SKSE::log::info("ClearSelectedIngredients: C++ idx {}, ret={}, count {} -> {}",
                    firstIdx, ok, prevSize, alchemyMenu->selectedIndexes.size());

                if (alchemyMenu->selectedIndexes.size() >= prevSize) {
                    break;
                }
            }
        }

        // Ensure flags and array are fully cleared
        for (auto& entry : alchemyMenu->ingredientEntries) {
            entry.isSelected = 0;
        }
        alchemyMenu->selectedIndexes.clear();

        RefreshAlchemyUI(alchemyMenu);
        return true;
    }

    AlchemySelectResult AlchemyController::SelectIngredients(
        const std::vector<std::string>& formIdStrings,
        bool autoCraft
    ) {
        std::vector<RE::FormID> formIds;
        formIds.reserve(formIdStrings.size());

        for (const auto& str : formIdStrings) {
            try {
                RE::FormID id = static_cast<RE::FormID>(std::stoul(str, nullptr, 0));
                formIds.push_back(id);
            } catch (const std::exception& e) {
                SKSE::log::warn("Invalid FormID string '{}': {}", str, e.what());
            }
        }

        return SelectIngredients(formIds, autoCraft);
    }

    AlchemySelectResult AlchemyController::SelectIngredients(
        const std::vector<RE::FormID>& formIds,
        bool autoCraft
    ) {
        AlchemySelectResult result;

        auto* alchemyMenu = GetAlchemyMenu();
        if (!alchemyMenu) {
            result.success = false;
            result.message = "Alchemy workbench is not open";
            return result;
        }

        auto* userEvents = RE::UserEvents::GetSingleton();
        if (!userEvents) {
            result.success = false;
            result.message = "UserEvents subsystem unavailable";
            return result;
        }

        auto* ui = RE::UI::GetSingleton();
        auto craftingMenu = (ui && ui->IsMenuOpen(RE::CraftingMenu::MENU_NAME))
            ? ui->GetMenu<RE::CraftingMenu>(RE::CraftingMenu::MENU_NAME)
            : nullptr;

        RE::GFxValue targetList = GetTargetItemList(alchemyMenu, craftingMenu.get());
        SKSE::log::info("SelectIngredients: requested {} FormIDs. targetList.isObj={}, initial selectedIndexes: {}",
            formIds.size(), targetList.IsObject(), alchemyMenu->selectedIndexes.size());

        if (formIds.empty()) {
            ClearSelectedIngredients();
            result.success = true;
            result.message = "Cleared ingredient selections";
            return result;
        }

        // 1. Clear currently selected ingredients
        ClearSelectedIngredients();
        SKSE::log::info("After Clear: selectedIndexes count: {}", alchemyMenu->selectedIndexes.size());

        // 2. Select each requested ingredient (up to 3)
        for (auto targetId : formIds) {
            if (result.selectedIngredients.size() >= 3) {
                break;
            }

            bool found = false;
            for (std::uint32_t i = 0; i < alchemyMenu->ingredientEntries.size(); ++i) {
                auto& entry = alchemyMenu->ingredientEntries[i];
                if (!entry.ingredient || !entry.ingredient->object) {
                    continue;
                }

                if (entry.ingredient->object->GetFormID() == targetId) {
                    found = true;

                    // (A) Trigger via Scaleform ActionScript ItemList (UI highlight & checkbox toggle)
                    if (targetList.IsObject()) {
                        int flashIndex = -1;
                        RE::GFxValue entryList;
                        if (targetList.GetMember("entryList", &entryList) && entryList.IsArray()) {
                            uint32_t count = entryList.GetArraySize();
                            for (uint32_t j = 0; j < count; ++j) {
                                RE::GFxValue entryObj;
                                entryList.GetElement(j, &entryObj);
                                if (entryObj.IsObject()) {
                                    RE::GFxValue formIdVal;
                                    if (entryObj.GetMember("formId", &formIdVal) || entryObj.GetMember("formID", &formIdVal)) {
                                        if (formIdVal.IsNumber() && static_cast<RE::FormID>(formIdVal.GetNumber()) == targetId) {
                                            flashIndex = static_cast<int>(j);
                                            break;
                                        }
                                    }
                                }
                            }
                        }

                        int chosenIndex = (flashIndex >= 0) ? flashIndex : static_cast<int>(i);
                        SKSE::log::info("Target 0x{:08X}: flashIdx={}, cppIdx={}", targetId, flashIndex, i);

                        RE::GFxValue indexVal(static_cast<double>(chosenIndex));
                        targetList.SetMember("selectedIndex", indexVal);

                        RE::GFxValue args[2];
                        args[0] = static_cast<double>(chosenIndex);
                        args[1] = 0.0;

                        targetList.Invoke("onItemSelect", nullptr, args, 1);
                        targetList.Invoke("onItemPress", nullptr, args, 2);
                        targetList.Invoke("UpdateList");
                    }

                    // (B) Also trigger via C++ engine UserEvents
                    alchemyMenu->currentIngredientIdx = i;
                    uint8_t wasSelected = entry.isSelected;
                    size_t countBefore = alchemyMenu->selectedIndexes.size();

                    bool retAccept = alchemyMenu->ProcessUserEvent(&userEvents->accept);
                    SKSE::log::info("Target 0x{:08X} (idx {}): ProcessUserEvent(&accept) ret={}, wasSel={}, isSel={}, count {} -> {}",
                        targetId, i, retAccept, wasSelected, entry.isSelected, countBefore, alchemyMenu->selectedIndexes.size());

                    if (alchemyMenu->selectedIndexes.size() == countBefore && !entry.isSelected) {
                        RE::BSFixedString acceptStr("Accept");
                        bool retStr = alchemyMenu->ProcessUserEvent(&acceptStr);
                        SKSE::log::info("  Trying BSFixedString('Accept'): ret={}, count={}", retStr, alchemyMenu->selectedIndexes.size());

                        if (alchemyMenu->selectedIndexes.size() == countBefore) {
                            RE::BSFixedString activateStr("Activate");
                            bool retAct = alchemyMenu->ProcessUserEvent(&activateStr);
                            SKSE::log::info("  Trying BSFixedString('Activate'): ret={}, count={}", retAct, alchemyMenu->selectedIndexes.size());
                        }
                    }

                    const char* name = entry.ingredient->object->GetName();
                    std::string itemName = (name && name[0] != '\0')
                        ? std::string(name)
                        : std::format("0x{:08X}", targetId);

                    result.selectedIngredients.push_back(itemName);
                    break;
                }
            }

            if (!found) {
                result.missingIngredients.push_back(std::format("0x{:08X}", targetId));
            }
        }

        // 3. Trigger UI Refresh
        RefreshAlchemyUI(alchemyMenu);

        // 4. Check selection outcome
        if (result.selectedIngredients.empty()) {
            result.success = false;
            result.message = "None of the requested ingredients are in player inventory";
            return result;
        }

        // 5. Perform auto-craft if requested and all ingredients were found
        if (autoCraft && result.missingIngredients.empty() && result.selectedIngredients.size() >= 2) {
            alchemyMenu->ProcessUserEvent(&userEvents->xButton);
            result.crafted = true;
            result.message = std::format("Crafted potion with {} ingredients", result.selectedIngredients.size());
        } else {
            result.message = std::format("Selected {} (active slots: {})",
                result.selectedIngredients.size(), alchemyMenu->selectedIndexes.size());
            if (!result.missingIngredients.empty()) {
                result.message += std::format(" ({} missing)", result.missingIngredients.size());
            }
        }

        result.success = true;
        return result;
    }

    MenuOpenCloseHandler* MenuOpenCloseHandler::GetSingleton() {
        static MenuOpenCloseHandler instance;
        return &instance;
    }

    void MenuOpenCloseHandler::Register() {
        auto* ui = RE::UI::GetSingleton();
        if (ui) {
            ui->AddEventSink<RE::MenuOpenCloseEvent>(this);
            SKSE::log::info("MenuOpenCloseHandler successfully registered with UI event source");
        }
    }

    RE::BSEventNotifyControl MenuOpenCloseHandler::ProcessEvent(
        const RE::MenuOpenCloseEvent* a_event,
        RE::BSTEventSource<RE::MenuOpenCloseEvent>*) {
        if (!a_event) return RE::BSEventNotifyControl::kContinue;

        if (a_event->menuName == RE::CraftingMenu::MENU_NAME) {
            bool opening = a_event->opening;
            SKSE::log::info("MenuOpenCloseEvent: CraftingMenu opening={}", opening);

            if (opening) {
                auto taskInterface = SKSE::GetTaskInterface();
                if (taskInterface) {
                    struct SubmenuCheckTask {
                        int retriesRemaining{ 15 };

                        void operator()() {
                            auto* alchemyMenu = AlchemyController::GetAlchemyMenu();
                            if (alchemyMenu) {
                                SKSE::log::info("CraftingMenu: AlchemyMenu confirmed via skyrim_cast! (retries left={})", retriesRemaining);
                                Server::WebSocketServer::GetSingleton().BroadcastFullSnapshot();
                                return;
                            }

                            // If CraftingMenu is still open, retry on the next frame (up to 15 frames, ~250ms)
                            auto* ui = RE::UI::GetSingleton();
                            if (ui && ui->IsMenuOpen(RE::CraftingMenu::MENU_NAME) && --retriesRemaining > 0) {
                                auto taskInterface = SKSE::GetTaskInterface();
                                if (taskInterface) {
                                    taskInterface->AddTask(*this);
                                }
                            } else {
                                SKSE::log::info("CraftingMenu: Active submenu is not AlchemyMenu (e.g. forge, enchanting, cooking).");
                            }
                        }
                    };

                    taskInterface->AddTask(SubmenuCheckTask{});
                }
            } else {
                nlohmann::json evt = {
                    { "type", "event" },
                    { "event", "alchemy.menu" },
                    { "isOpen", false },
                    { "ts", std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::system_clock::now().time_since_epoch()).count() }
                };
                Server::WebSocketServer::GetSingleton().Broadcast(evt);
            }
        }

        return RE::BSEventNotifyControl::kContinue;
    }

}
