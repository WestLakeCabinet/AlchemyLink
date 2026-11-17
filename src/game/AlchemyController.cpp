#include "game/AlchemyController.h"
#include "server/WebSocketServer.h"
#include <chrono>
#include <future>
#include <thread>
#include <atomic>

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

    bool AlchemyController::IsAlchemyItemListReady() {
        auto* alchemyMenu = GetAlchemyMenu();
        if (!alchemyMenu) {
            return false;
        }

        // 1. C++ 엔진 레벨에서 재료 배열이 채워졌는지 확인
        const size_t cppCount = alchemyMenu->ingredientEntries.size();
        if (cppCount == 0) {
            return false;
        }

        auto* ui = RE::UI::GetSingleton();
        auto craftingMenu = (ui && ui->IsMenuOpen(RE::CraftingMenu::MENU_NAME))
            ? ui->GetMenu<RE::CraftingMenu>(RE::CraftingMenu::MENU_NAME)
            : nullptr;

        auto* movie = craftingMenu ? craftingMenu->uiMovie.get() : (alchemyMenu->view ? alchemyMenu->view.get() : nullptr);

        // 2. Scaleform craftingMenu / inventoryLists의 bInCategories 확인 (바닐라 및 SkyUI 공통 플래그)
        // bInCategories가 true이면 아직 좌측 카테고리(모두 / 물약 / 독) 선택 상태임!
        RE::GFxValue inCategoriesVal;
        if (alchemyMenu->craftingMenu.IsObject()) {
            if (alchemyMenu->craftingMenu.GetMember("bInCategories", &inCategoriesVal) && inCategoriesVal.IsBool()) {
                if (inCategoriesVal.GetBool()) {
                    return false;
                }
            }
        }

        if (movie) {
            if ((movie->GetVariable(&inCategoriesVal, "_root.Menu.bInCategories") ||
                 movie->GetVariable(&inCategoriesVal, "_root.CraftingMenu.bInCategories") ||
                 movie->GetVariable(&inCategoriesVal, "_root.Menu_mc.bInCategories") ||
                 movie->GetVariable(&inCategoriesVal, "_root.Menu_mc.inventoryLists.bInCategories") ||
                 movie->GetVariable(&inCategoriesVal, "_root.InventoryLists.bInCategories") ||
                 movie->GetVariable(&inCategoriesVal, "_root.Menu.InventoryLists.bInCategories")) &&
                inCategoriesVal.IsBool() && inCategoriesVal.GetBool()) {
                return false;
            }

            // SkyUI inventoryLists._currentState 확인 (1 = SHOW_CATEGORIES, 2 = SHOW_PANEL)
            RE::GFxValue panelState;
            if (movie->GetVariable(&panelState, "_root.Menu_mc.inventoryLists._currentState") ||
                movie->GetVariable(&panelState, "_root.Menu.InventoryLists._currentState") ||
                movie->GetVariable(&panelState, "_root.CraftingMenu.InventoryLists._currentState")) {
                if (panelState.IsNumber() && static_cast<int>(panelState.GetNumber()) < 2) {
                    return false;
                }
            }

            // SkyUI categoryList 포커스 여부
            RE::GFxValue catFocus;
            if ((movie->GetVariable(&catFocus, "_root.Menu_mc.inventoryLists.categoryList.bHasFocus") ||
                 movie->GetVariable(&catFocus, "_root.Menu.InventoryLists.categoryList.bHasFocus") ||
                 movie->GetVariable(&catFocus, "_root.CraftingMenu.InventoryLists.categoryList.bHasFocus")) &&
                catFocus.IsBool() && catFocus.GetBool()) {
                return false;
            }
        }

        // 3. categories 리스트가 포커스를 가지고 있는지 확인
        if (alchemyMenu->categories.IsObject()) {
            RE::GFxValue catFocus;
            if (alchemyMenu->categories.GetMember("bHasFocus", &catFocus) && catFocus.IsBool() && catFocus.GetBool()) {
                return false;
            }
        }

        // 4. itemList 객체 상태 및 포커스 검사
        RE::GFxValue targetList = GetTargetItemList(alchemyMenu, craftingMenu.get());
        if (!targetList.IsObject()) {
            return false;
        }

        // SkyUI에서는 카테고리 단계일 때 itemList의 disableInput 또는 disableSelection이 true로 설정됨
        RE::GFxValue disableVal;
        if (targetList.GetMember("disableInput", &disableVal) && disableVal.IsBool() && disableVal.GetBool()) {
            return false;
        }
        if (targetList.GetMember("disableSelection", &disableVal) && disableVal.IsBool() && disableVal.GetBool()) {
            return false;
        }

        // itemList의 bHasFocus가 명시적인 bool이고 false인 경우 (카테고리 뷰 포커스)
        RE::GFxValue itemFocus;
        if (targetList.GetMember("bHasFocus", &itemFocus) && itemFocus.IsBool() && !itemFocus.GetBool()) {
            return false;
        }

        // itemList가 숨겨져 있는지(_visible == false)
        RE::GFxValue visibleVal;
        if (targetList.GetMember("_visible", &visibleVal) && visibleVal.IsBool() && !visibleVal.GetBool()) {
            return false;
        }

        // itemList 알파값이 0인지
        RE::GFxValue alphaVal;
        if (targetList.GetMember("_alpha", &alphaVal) && alphaVal.IsNumber() && alphaVal.GetNumber() <= 0.0) {
            return false;
        }

        // entryList 또는 _dataProvider 배열 확인
        uint32_t flashCount = 0;
        RE::GFxValue listArray;
        if (targetList.GetMember("entryList", &listArray) && listArray.IsArray()) {
            flashCount = listArray.GetArraySize();
        } else if (targetList.GetMember("_dataProvider", &listArray) && listArray.IsArray()) {
            flashCount = listArray.GetArraySize();
        } else if (targetList.GetMember("dataProvider", &listArray) && listArray.IsArray()) {
            flashCount = listArray.GetArraySize();
        } else {
            return false;
        }

        if (flashCount == 0) {
            return false;
        }

        return true;
    }

    void RefreshAlchemyUI(RE::CraftingSubMenus::AlchemyMenu* alchemyMenu) {
        if (!alchemyMenu) return;

        if (alchemyMenu->itemList.IsObject()) {
            alchemyMenu->itemList.Invoke("UpdateList");
        } else if (alchemyMenu->view) {
            alchemyMenu->view->Invoke("_root.CraftingMenu.InventoryLists.itemList.UpdateList", nullptr, nullptr, 0);
            alchemyMenu->view->Invoke("_root.Menu_mc.inventoryLists.itemList.UpdateList", nullptr, nullptr, 0);
        }
    }

    bool AlchemyController::ClearSelectedIngredients() {
        return SelectIngredients(std::vector<RE::FormID>{}, false).success;
    }

    std::vector<RE::FormID> AlchemyController::GetSelectedIngredientFormIDs() {
        std::vector<RE::FormID> result;
        auto* alchemyMenu = GetAlchemyMenu();
        if (!alchemyMenu) {
            return result;
        }

        for (std::uint32_t idx : alchemyMenu->selectedIndexes) {
            if (idx < alchemyMenu->ingredientEntries.size()) {
                auto& entry = alchemyMenu->ingredientEntries[idx];
                if (entry.ingredient && entry.ingredient->object) {
                    result.push_back(entry.ingredient->object->GetFormID());
                }
            }
        }
        return result;
    }

    AlchemySelectResult AlchemyController::SelectIngredients(
        const std::vector<std::string>& formIdStrings,
        bool autoCraft
    ) {
        std::vector<RE::FormID> formIds;
        formIds.reserve(formIdStrings.size());

        for (const auto& str : formIdStrings) {
            try {
                // Support both 32-bit and 64-bit sign-extended hex formats safely
                uint64_t val = std::stoull(str, nullptr, 0);
                RE::FormID id = static_cast<RE::FormID>(val & 0xFFFFFFFFULL);
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

        if (!IsAlchemyItemListReady()) {
            result.success = false;
            result.message = "Alchemy item list is not active (still in categories or transitioning)";
            return result;
        }

        auto* ui = RE::UI::GetSingleton();
        auto craftingMenu = (ui && ui->IsMenuOpen(RE::CraftingMenu::MENU_NAME))
            ? ui->GetMenu<RE::CraftingMenu>(RE::CraftingMenu::MENU_NAME)
            : nullptr;

        RE::GFxValue targetList = GetTargetItemList(alchemyMenu, craftingMenu.get());
        if (!targetList.IsObject()) {
            result.success = false;
            result.message = "Target item list not found";
            return result;
        }

        // 1. Gather currently selected FormIDs & their entry indices
        std::map<RE::FormID, std::uint32_t> currentSelectedMap;
        for (std::uint32_t idx : alchemyMenu->selectedIndexes) {
            if (idx < alchemyMenu->ingredientEntries.size()) {
                auto& entry = alchemyMenu->ingredientEntries[idx];
                if (entry.ingredient && entry.ingredient->object) {
                    currentSelectedMap[entry.ingredient->object->GetFormID()] = idx;
                }
            }
        }
        for (std::uint32_t i = 0; i < alchemyMenu->ingredientEntries.size(); ++i) {
            auto& entry = alchemyMenu->ingredientEntries[i];
            if (entry.isSelected && entry.ingredient && entry.ingredient->object) {
                currentSelectedMap[entry.ingredient->object->GetFormID()] = i;
            }
        }

        // 2. Prepare target FormIDs (deduplicated, max 3)
        std::vector<RE::FormID> targetFormIds;
        for (auto id : formIds) {
            if (targetFormIds.size() >= 3) break;
            if (std::find(targetFormIds.begin(), targetFormIds.end(), id) == targetFormIds.end()) {
                targetFormIds.push_back(id);
            }
        }

        // 3. Compute Diff
        // Items to remove: currently selected but not in target
        std::vector<std::pair<RE::FormID, std::uint32_t>> toRemove;
        for (const auto& [currId, currIdx] : currentSelectedMap) {
            if (std::find(targetFormIds.begin(), targetFormIds.end(), currId) == targetFormIds.end()) {
                toRemove.emplace_back(currId, currIdx);
            }
        }

        // Items to add: in target but not currently selected
        std::vector<RE::FormID> toAdd;
        for (auto targetId : targetFormIds) {
            if (currentSelectedMap.find(targetId) == currentSelectedMap.end()) {
                toAdd.push_back(targetId);
            }
        }

        SKSE::log::info("SelectIngredients Diff: curr={}, target={}, remove={}, add={}",
            currentSelectedMap.size(), targetFormIds.size(), toRemove.size(), toAdd.size());

        // Fast-path: if diff is empty, nothing to toggle!
        if (toRemove.empty() && toAdd.empty()) {
            for (auto id : targetFormIds) {
                auto it = currentSelectedMap.find(id);
                if (it != currentSelectedMap.end() && it->second < alchemyMenu->ingredientEntries.size()) {
                    auto& entry = alchemyMenu->ingredientEntries[it->second];
                    const char* name = (entry.ingredient && entry.ingredient->object) ? entry.ingredient->object->GetName() : nullptr;
                    result.selectedIngredients.push_back(name ? name : std::format("0x{:08X}", id));
                }
            }
            result.success = true;
            result.message = "Selections already match";
            return result;
        }

        // 4. Untoggle items via Scaleform onItemPress (natural engine untoggle)
        for (const auto& [remId, remIdx] : toRemove) {
            SKSE::log::info("SelectIngredients: Untoggling 0x{:08X} at index {}", remId, remIdx);
            RE::GFxValue indexVal(static_cast<double>(remIdx));
            targetList.SetMember("selectedIndex", indexVal);

            RE::GFxValue args[2];
            args[0] = static_cast<double>(remIdx);
            args[1] = 0.0;
            targetList.Invoke("onItemSelect", nullptr, args, 1);
            targetList.Invoke("onItemPress", nullptr, args, 2);
        }

        // 5. Toggle ON items to add via Scaleform onItemPress
        for (auto addId : toAdd) {
            bool found = false;
            for (std::uint32_t i = 0; i < alchemyMenu->ingredientEntries.size(); ++i) {
                auto& entry = alchemyMenu->ingredientEntries[i];
                if (!entry.ingredient || !entry.ingredient->object) continue;

                if (entry.ingredient->object->GetFormID() == addId) {
                    found = true;
                    SKSE::log::info("SelectIngredients: Toggling ON 0x{:08X} at index {}", addId, i);
                    RE::GFxValue indexVal(static_cast<double>(i));
                    targetList.SetMember("selectedIndex", indexVal);

                    RE::GFxValue args[2];
                    args[0] = static_cast<double>(i);
                    args[1] = 0.0;
                    targetList.Invoke("onItemSelect", nullptr, args, 1);
                    targetList.Invoke("onItemPress", nullptr, args, 2);

                    const char* name = entry.ingredient->object->GetName();
                    result.selectedIngredients.push_back((name && name[0] != '\0') ? name : std::format("0x{:08X}", addId));
                    break;
                }
            }
            if (!found) {
                result.missingIngredients.push_back(std::format("0x{:08X}", addId));
            }
        }

        // 6. Refresh UI once at the end
        targetList.Invoke("UpdateList");
        RefreshAlchemyUI(alchemyMenu);

        // Include retained items in the result
        for (auto targetId : targetFormIds) {
            if (std::find(toAdd.begin(), toAdd.end(), targetId) == toAdd.end()) {
                auto it = currentSelectedMap.find(targetId);
                if (it != currentSelectedMap.end() && it->second < alchemyMenu->ingredientEntries.size()) {
                    auto& entry = alchemyMenu->ingredientEntries[it->second];
                    const char* name = (entry.ingredient && entry.ingredient->object) ? entry.ingredient->object->GetName() : nullptr;
                    result.selectedIngredients.push_back(name ? name : std::format("0x{:08X}", targetId));
                }
            }
        }

        // 7. Auto-craft if requested
        if (autoCraft && result.missingIngredients.empty() && result.selectedIngredients.size() >= 2) {
            auto* userEvents = RE::UserEvents::GetSingleton();
            if (userEvents) {
                alchemyMenu->ProcessUserEvent(&userEvents->xButton);
                result.crafted = true;
                result.message = std::format("Crafted potion with {} ingredients", result.selectedIngredients.size());
            }
        } else {
            result.message = std::format("Selected {} (active slots: {})",
                result.selectedIngredients.size(), alchemyMenu->selectedIndexes.size());
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

    struct MonitorSession {
        std::atomic<bool> active{ true };
        int checkCount{ 0 };
        bool wasListReady{ false };
        std::vector<RE::FormID> lastSelected;
    };

    static std::shared_ptr<MonitorSession> s_monitorSession;
    static std::mutex s_sessionMutex;

    RE::BSEventNotifyControl MenuOpenCloseHandler::ProcessEvent(
        const RE::MenuOpenCloseEvent* a_event,
        RE::BSTEventSource<RE::MenuOpenCloseEvent>*) {
        if (!a_event) return RE::BSEventNotifyControl::kContinue;

        if (a_event->menuName == RE::CraftingMenu::MENU_NAME) {
            bool opening = a_event->opening;
            SKSE::log::info("MenuOpenCloseEvent: CraftingMenu opening={}", opening);

            if (opening) {
                std::shared_ptr<MonitorSession> session;
                {
                    std::lock_guard<std::mutex> lock(s_sessionMutex);
                    if (s_monitorSession) {
                        s_monitorSession->active.store(false);
                    }
                    s_monitorSession = std::make_shared<MonitorSession>();
                    session = s_monitorSession;
                }

                std::thread([session]() {
                    while (session->active.load()) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(250));

                        if (!session->active.load()) {
                            break;
                        }

                        auto* taskInterface = SKSE::GetTaskInterface();
                        if (!taskInterface) {
                            break;
                        }

                        auto promise = std::make_shared<std::promise<bool>>();
                        auto future = promise->get_future();

                        taskInterface->AddTask([session, promise]() {
                            if (!session->active.load()) {
                                promise->set_value(false);
                                return;
                            }

                            ++session->checkCount;

                            auto* ui = RE::UI::GetSingleton();
                            if (!ui || !ui->IsMenuOpen(RE::CraftingMenu::MENU_NAME)) {
                                promise->set_value(false);
                                return;
                            }

                            auto* alchemyMenu = AlchemyController::GetAlchemyMenu();
                            if (!alchemyMenu) {
                                if (session->checkCount >= 10) {
                                    SKSE::log::info("CraftingMenu: Active submenu is not AlchemyMenu. Stopping monitor.");
                                    promise->set_value(false);
                                } else {
                                    promise->set_value(true);
                                }
                                return;
                            }

                            bool isReady = AlchemyController::IsAlchemyItemListReady();

                            int64_t nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::system_clock::now().time_since_epoch()).count();

                            if (isReady && !session->wasListReady) {
                                session->wasListReady = true;
                                session->lastSelected = AlchemyController::GetSelectedIngredientFormIDs();
                                SKSE::log::info("CraftingMenu: Alchemy ItemList OPENED! (cppEntries={}, check={})",
                                    alchemyMenu->ingredientEntries.size(), session->checkCount);

                                std::vector<std::string> hexSelected;
                                for (auto id : session->lastSelected) {
                                    hexSelected.push_back(std::format("0x{:08X}", id));
                                }

                                nlohmann::json evt = {
                                    { "type", "event" },
                                    { "event", "alchemy.menu" },
                                    { "isOpen", true },
                                    { "selectedIngredients", hexSelected },
                                    { "ts", nowMs }
                                };
                                Server::WebSocketServer::GetSingleton().Broadcast(evt);
                            } else if (!isReady && session->wasListReady) {
                                session->wasListReady = false;
                                session->lastSelected.clear();
                                SKSE::log::info("CraftingMenu: Alchemy ItemList CLOSED (back to categories).");

                                nlohmann::json evt = {
                                    { "type", "event" },
                                    { "event", "alchemy.menu" },
                                    { "isOpen", false },
                                    { "selectedIngredients", nlohmann::json::array() },
                                    { "ts", nowMs }
                                };
                                Server::WebSocketServer::GetSingleton().Broadcast(evt);
                            } else if (isReady && session->wasListReady) {
                                auto currentSelected = AlchemyController::GetSelectedIngredientFormIDs();
                                if (currentSelected != session->lastSelected) {
                                    session->lastSelected = currentSelected;

                                    std::vector<std::string> hexSelected;
                                    for (auto id : session->lastSelected) {
                                        hexSelected.push_back(std::format("0x{:08X}", id));
                                    }

                                    SKSE::log::info("CraftingMenu: Alchemy selection changed in Skyrim (count={})", hexSelected.size());
                                    nlohmann::json evt = {
                                        { "type", "event" },
                                        { "event", "alchemy.selection" },
                                        { "selectedIngredients", hexSelected },
                                        { "ts", nowMs }
                                    };
                                    Server::WebSocketServer::GetSingleton().Broadcast(evt);
                                }
                            }

                            promise->set_value(true);
                        });

                        if (future.wait_for(std::chrono::seconds(2)) == std::future_status::ready) {
                            if (!future.get()) {
                                break;
                            }
                        }
                    }

                    session->active.store(false);
                }).detach();

            } else {
                {
                    std::lock_guard<std::mutex> lock(s_sessionMutex);
                    if (s_monitorSession) {
                        s_monitorSession->active.store(false);
                        s_monitorSession.reset();
                    }
                }

                int64_t nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count();
                nlohmann::json evt = {
                    { "type", "event" },
                    { "event", "alchemy.menu" },
                    { "isOpen", false },
                    { "selectedIngredients", nlohmann::json::array() },
                    { "ts", nowMs }
                };
                Server::WebSocketServer::GetSingleton().Broadcast(evt);
            }
        }

        return RE::BSEventNotifyControl::kContinue;
    }

}
