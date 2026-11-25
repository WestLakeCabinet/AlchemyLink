#include "game/AlchemyController.h"
#include "game/InventoryExtractor.h"   // 추가
#include "server/WebSocketServer.h"
#include "sync/ProtoPacketBuilder.h"   // 추가
#include "RE/M/MessageBoxMenu.h"
#include "RE/M/MessageBoxData.h"
#include <chrono>
#include <future>
#include <thread>
#include <atomic>

namespace SkyPotion::Game {

    static std::atomic<bool> s_craftCancel{ false };
    static std::atomic<bool> s_isCrafting{ false };

    RE::CraftingSubMenus::AlchemyMenu* AlchemyController::GetAlchemyMenu() {
        if (!Util::IsGameSessionReady()) {
            return nullptr;
        }

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

    bool AlchemyController::IsCrafting() {
        return s_isCrafting.load();
    }

    bool AlchemyController::StopCrafting() {
        SKSE::log::info("AlchemyController StopCrafting");
        if (s_isCrafting.load()) {
            s_craftCancel.store(true);
            SKSE::log::info("AlchemyController: StopCrafting requested");
            return true;
        }
        return false;
    }

    void AlchemyController::DismissOpenMessageBoxes() {
        int dismissed = 0;
        while (dismissed < 50 && RE::MessageBoxMenu::GetCurrentMessageBoxData() != nullptr) {
            auto* msgData = RE::MessageBoxMenu::GetCurrentMessageBoxData();
            std::string text;
            if (msgData && msgData->bodyText.c_str() != nullptr) {
                text = msgData->bodyText.c_str();
            }

            // DO NOT auto-dismiss exit/quit confirmation dialogs!
            if (text.find("Quit") != std::string::npos ||
                text.find("quit") != std::string::npos ||
                text.find("종료") != std::string::npos ||
                text.find("나가") != std::string::npos) {
                SKSE::log::info("DismissOpenMessageBoxes: Skipping quit/exit confirmation: '{}'", text);
                break;
            }

            SKSE::log::info("DismissOpenMessageBoxes: Auto-dismissing MessageBoxMenu ({}): '{}'", dismissed + 1, text);

            RE::MessageBoxMenu::SelectOption(0);
            dismissed++;
        }
        if (dismissed > 0) {
            SKSE::log::info("DismissOpenMessageBoxes: Successfully auto-dismissed {} popups", dismissed);
        }
    }

    RE::GFxValue GetTargetItemList(RE::CraftingSubMenus::AlchemyMenu* alchemyMenu, RE::CraftingMenu* craftingMenu) {
        if (alchemyMenu && alchemyMenu->itemList.IsObject()) {
            return alchemyMenu->itemList;
        }

        RE::GFxValue list;
        auto* movie = craftingMenu ? craftingMenu->uiMovie.get() : (alchemyMenu ? alchemyMenu->view.get() : nullptr);
        if (movie) {
            if (movie->GetVariable(&list, "_root.Menu.InventoryLists.ItemList") && list.IsObject()) return list;
            if (movie->GetVariable(&list, "_root.Menu.InventoryLists._itemList") && list.IsObject()) return list;
            if (movie->GetVariable(&list, "_root.Menu.InventoryLists.itemList") && list.IsObject()) return list;
            if (movie->GetVariable(&list, "_root.Menu.inventoryLists._itemList") && list.IsObject()) return list;
            if (movie->GetVariable(&list, "_root.Menu.inventoryLists.itemList") && list.IsObject()) return list;
            if (movie->GetVariable(&list, "_root.Menu.itemList") && list.IsObject()) return list;
            if (movie->GetVariable(&list, "_root.Menu._itemList") && list.IsObject()) return list;
            if (movie->GetVariable(&list, "_root.CraftingMenu.InventoryLists._itemList") && list.IsObject()) return list;
            if (movie->GetVariable(&list, "_root.CraftingMenu.InventoryLists.itemList") && list.IsObject()) return list;
            if (movie->GetVariable(&list, "_root.Menu_mc.inventoryLists.itemList") && list.IsObject()) return list;
            if (movie->GetVariable(&list, "_root.CraftingMenu.inventoryLists.itemList") && list.IsObject()) return list;
        }
        return list;
    }

    static bool IsSkyUICraftingMenu(RE::GFxMovieView* movie, const RE::GFxValue& targetList) {
        RE::GFxValue v;
        if (movie) {
            if ((movie->GetVariable(&v, "_global.skyui") && v.IsObject()) ||
                (movie->GetVariable(&v, "_root.Menu.InventoryLists.categoryList") && v.IsObject()) ||
                (movie->GetVariable(&v, "_root.Menu.InventoryLists.searchWidget") && v.IsObject())) {
                return true;
            }
        }
        if (targetList.IsObject()) {
            RE::GFxValue listRef = targetList;
            if ((listRef.GetMember("listState", &v) && v.IsObject()) ||
                (listRef.GetMember("_entryList", &v) && v.IsArray())) {
                return true;
            }
        }
        return false;
    }

    static bool GetListEntryArray(RE::GFxValue& targetList, RE::GFxValue& outArray) {
        if (!targetList.IsObject()) return false;
        if (targetList.GetMember("entryList", &outArray) && outArray.IsArray()) return true;
        if (targetList.GetMember("_entryList", &outArray) && outArray.IsArray()) return true;
        if (targetList.GetMember("_dataProvider", &outArray) && outArray.IsArray()) return true;
        if (targetList.GetMember("dataProvider", &outArray) && outArray.IsArray()) return true;
        return false;
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

        // 2. itemList 객체 확인
        RE::GFxValue targetList = GetTargetItemList(alchemyMenu, craftingMenu.get());
        if (!targetList.IsObject()) {
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

        // SkyUI에서는 카테고리 목록과 재료 목록이 동시에 표시되므로,
        // categoryList에 포커스가 있거나 검색 필터로 entryList가 비어 있어도 재료 선택/제조가 가능함.
        if (IsSkyUICraftingMenu(movie, targetList)) {
            return true;
        }

        // 3. 바닐라 메뉴: Scaleform craftingMenu / inventoryLists의 bInCategories 확인
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
        }

        // categories 리스트가 포커스를 가지고 있는지 확인
        if (alchemyMenu->categories.IsObject()) {
            RE::GFxValue catFocus;
            if (alchemyMenu->categories.GetMember("bHasFocus", &catFocus) && catFocus.IsBool() && catFocus.GetBool()) {
                return false;
            }
        }

        // itemList의 disableInput / disableSelection 확인
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

        // entryList 또는 _dataProvider 배열 확인
        RE::GFxValue listArray;
        if (!GetListEntryArray(targetList, listArray) || listArray.GetArraySize() == 0) {
            return false;
        }

        return true;
    }

    void RefreshAlchemyUI(RE::CraftingSubMenus::AlchemyMenu* alchemyMenu, RE::GFxValue* targetList = nullptr) {
        if (!alchemyMenu) return;

        if (targetList && targetList->IsObject()) {
            targetList->Invoke("UpdateList");
            targetList->Invoke("InvalidateData");
        }

        if (alchemyMenu->itemList.IsObject()) {
            alchemyMenu->itemList.Invoke("UpdateList");
            alchemyMenu->itemList.Invoke("InvalidateData");
        } else if (alchemyMenu->view) {
            alchemyMenu->view->Invoke("_root.Menu.InventoryLists._itemList.InvalidateData", nullptr, nullptr, 0);
            alchemyMenu->view->Invoke("_root.Menu.InventoryLists._itemList.UpdateList", nullptr, nullptr, 0);
            alchemyMenu->view->Invoke("_root.Menu.InventoryLists.itemList.InvalidateData", nullptr, nullptr, 0);
            alchemyMenu->view->Invoke("_root.Menu.InventoryLists.itemList.UpdateList", nullptr, nullptr, 0);
            alchemyMenu->view->Invoke("_root.CraftingMenu.InventoryLists.itemList.UpdateList", nullptr, nullptr, 0);
            alchemyMenu->view->Invoke("_root.Menu_mc.inventoryLists.itemList.UpdateList", nullptr, nullptr, 0);
        }
    }

    static std::int32_t FindDisplayIndexForCppIndex(
        RE::GFxValue& targetList,
        std::uint32_t cppIdx,
        RE::FormID formId,
        const char* ingredientName
    ) {
        RE::GFxValue listArray;
        if (!GetListEntryArray(targetList, listArray)) {
            return -1;
        }

        const std::uint32_t size = listArray.GetArraySize();
        for (std::uint32_t k = 0; k < size; ++k) {
            RE::GFxValue elem;
            if (!listArray.GetElement(k, &elem) || !elem.IsObject()) {
                continue;
            }

            RE::GFxValue itemIdxVal;
            if (elem.GetMember("itemIndex", &itemIdxVal) && itemIdxVal.IsNumber()) {
                if (static_cast<std::uint32_t>(itemIdxVal.GetNumber()) == cppIdx) {
                    return static_cast<std::int32_t>(k);
                }
                continue;
            }

            if (formId != 0) {
                RE::GFxValue formIdVal;
                if (elem.GetMember("formId", &formIdVal) && formIdVal.IsNumber()) {
                    if (static_cast<RE::FormID>(formIdVal.GetNumber()) == formId) {
                        return static_cast<std::int32_t>(k);
                    }
                    continue;
                }
            }

            if (ingredientName && ingredientName[0] != '\0') {
                RE::GFxValue textVal;
                if (elem.GetMember("text", &textVal) && textVal.IsString()) {
                    const char* entryText = textVal.GetString();
                    if (entryText && std::strcmp(entryText, ingredientName) == 0) {
                        return static_cast<std::int32_t>(k);
                    }
                }
            }
        }

        return -1;
    }

    static bool ToggleIngredientEntry(
        RE::CraftingSubMenus::AlchemyMenu* alchemyMenu,
        RE::CraftingMenu* craftingMenu,
        RE::GFxValue& targetList,
        std::uint32_t cppIdx,
        RE::FormID formId
    ) {
        if (!alchemyMenu || cppIdx >= alchemyMenu->ingredientEntries.size()) {
            return false;
        }

        auto& entry = alchemyMenu->ingredientEntries[cppIdx];
        const char* ingredientName = (entry.ingredient && entry.ingredient->object)
            ? entry.ingredient->object->GetName()
            : nullptr;
        const bool wasSelected = (entry.isSelected != 0);

        auto isToggled = [&]() -> bool {
            return (alchemyMenu->ingredientEntries[cppIdx].isSelected != 0) != wasSelected;
        };

        auto* movie = craftingMenu ? craftingMenu->uiMovie.get() : (alchemyMenu->view ? alchemyMenu->view.get() : nullptr);

        // 1. Scaleform 리스트에서 해당 C++ 인덱스에 대응하는 화면 표시 인덱스(displayIdx) 탐색
        std::int32_t displayIdx = FindDisplayIndexForCppIndex(targetList, cppIdx, formId, ingredientName);
        if (displayIdx >= 0) {
            RE::GFxValue prevDisableInput;
            RE::GFxValue prevDisableSelection;
            bool hadDisableInput = targetList.GetMember("disableInput", &prevDisableInput) && prevDisableInput.IsBool() && prevDisableInput.GetBool();
            bool hadDisableSelection = targetList.GetMember("disableSelection", &prevDisableSelection) && prevDisableSelection.IsBool() && prevDisableSelection.GetBool();

            if (hadDisableInput) {
                targetList.SetMember("disableInput", RE::GFxValue(false));
            }
            if (hadDisableSelection) {
                targetList.SetMember("disableSelection", RE::GFxValue(false));
            }

            RE::GFxValue indexVal(static_cast<double>(displayIdx));
            targetList.SetMember("selectedIndex", indexVal);

            // 1a. 바닐라 BSScrollingList / SkyUI ScrollingList
            RE::GFxValue args[2];
            args[0] = static_cast<double>(displayIdx);
            args[1] = 0.0;
            targetList.Invoke("onItemSelect", nullptr, args, 1);
            targetList.Invoke("onItemPress", nullptr, args, 2);

            // 1b. SkyUI InventoryLists.onItemSelect(cppIdx)
            if (!isToggled() && movie) {
                RE::GFxValue cppArg(static_cast<double>(cppIdx));
                movie->Invoke("_root.Menu.InventoryLists.onItemSelect", nullptr, &cppArg, 1);
                if (!isToggled()) {
                    movie->Invoke("_root.Menu.inventoryLists.onItemSelect", nullptr, &cppArg, 1);
                }
            }

            // 1c. SkyUI EventDispatcher itemPress 이벤트 직접 디스패치
            if (!isToggled() && movie) {
                RE::GFxValue listArray;
                RE::GFxValue entryObj;
                if (GetListEntryArray(targetList, listArray) &&
                    static_cast<std::uint32_t>(displayIdx) < listArray.GetArraySize()) {
                    listArray.GetElement(static_cast<std::uint32_t>(displayIdx), &entryObj);
                }

                RE::GFxValue evtObj;
                movie->CreateObject(&evtObj);
                if (evtObj.IsObject()) {
                    evtObj.SetMember("type", RE::GFxValue("itemPress"));
                    evtObj.SetMember("index", RE::GFxValue(static_cast<double>(displayIdx)));
                    evtObj.SetMember("keyboardOrMouse", RE::GFxValue(0.0));
                    if (entryObj.IsObject()) {
                        evtObj.SetMember("entry", entryObj);
                    }
                    targetList.Invoke("dispatchEvent", nullptr, &evtObj, 1);
                }
            }

            if (hadDisableInput) {
                targetList.SetMember("disableInput", RE::GFxValue(true));
            }
            if (hadDisableSelection) {
                targetList.SetMember("disableSelection", RE::GFxValue(true));
            }
        }

        // 2. SkyUI 카테고리/검색 필터로 화면에 없거나 Scaleform 이벤트가 동작하지 않은 경우 FxDelegate 직접 호출
        if (!isToggled() && craftingMenu && craftingMenu->fxDelegate) {
            auto tryCallback = [&](const char* name, double argIndex) -> bool {
                if (auto* defn = craftingMenu->fxDelegate->callbacks.Get(name)) {
                    if (defn->callback && defn->handler) {
                        auto* movie = craftingMenu->uiMovie.get();
                        RE::GFxValue arg(argIndex);
                        RE::FxDelegateArgs cbArgs(RE::GFxValue(), defn->handler.get(), movie, &arg, 1);
                        defn->callback(cbArgs);
                        return (alchemyMenu->ingredientEntries[cppIdx].isSelected != 0) != wasSelected;
                    }
                }
                return false;
            };

            if (!tryCallback("ChooseIngredient", static_cast<double>(cppIdx))) {
                if (displayIdx >= 0) {
                    tryCallback("ItemSelect", static_cast<double>(displayIdx));
                }
            }

            if (!isToggled()) {
                static bool s_loggedCallbacks = false;
                if (!s_loggedCallbacks) {
                    s_loggedCallbacks = true;
                    std::string keys;
                    for (auto it = craftingMenu->fxDelegate->callbacks.begin(); it != craftingMenu->fxDelegate->callbacks.end(); ++it) {
                        if (!keys.empty()) keys += ", ";
                        keys += it->first.c_str();
                    }
                    SKSE::log::warn("ToggleIngredientEntry: Fallback failed (displayIdx={}, cppIdx={}). Registered FxDelegate callbacks: [{}]",
                        displayIdx, cppIdx, keys);
                }
            }
        }

        return isToggled();
    }

    bool AlchemyController::ClearSelectedIngredients() {
        StopCrafting();
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

    AlchemySelectResult AlchemyController::CraftPotions(int count) {
        AlchemySelectResult result;
        auto* alchemyMenu = GetAlchemyMenu();
        if (!alchemyMenu) {
            result.success = false;
            result.message = "Alchemy workbench is not open";
            return result;
        }

        if (alchemyMenu->selectedIndexes.size() < 2) {
            result.success = false;
            result.message = "Not enough ingredients selected to craft";
            return result;
        }

        for (std::uint32_t idx : alchemyMenu->selectedIndexes) {
            if (idx < alchemyMenu->ingredientEntries.size()) {
                auto& entry = alchemyMenu->ingredientEntries[idx];
                const char* name = (entry.ingredient && entry.ingredient->object) ? entry.ingredient->object->GetName() : nullptr;
                if (name) result.selectedIngredients.push_back(name);
            }
        }

        if (count <= 1) {
            auto* userEvents = RE::UserEvents::GetSingleton();
            if (userEvents) {
                AlchemyController::DismissOpenMessageBoxes();
                alchemyMenu->ProcessUserEvent(&userEvents->xButton);
                AlchemyController::DismissOpenMessageBoxes();

                result.crafted = true;
                result.craftedCount = 1;
                result.success = true;
                result.message = "Crafted 1 potion";
                return result;
            }
            result.success = false;
            result.message = "UserEvents not found";
            return result;
        }

        // Prevent starting a new bulk craft if one is already in progress (avoids deadlock)
        if (s_isCrafting.load()) {
            SKSE::log::warn("CraftPotions: Bulk crafting is already in progress");
            result.success = false;
            result.crafted = false;
            result.craftedCount = 0;
            result.message = "Crafting already in progress";
            return result;
        }

        s_craftCancel.store(false);
        s_isCrafting.store(true);

        std::thread([count]() {
            int crafted = 0;
            SKSE::log::info("CraftPotions: Starting bulk craft of up to {} potions", count);
            for (int i = 0; i < count; ++i) {
                if (s_craftCancel.load() || SkyPotion::g_isShuttingDown.load()) {
                    SKSE::log::info("CraftPotions: Cancelled after {} potions", crafted);
                    break;
                }

                auto promise = std::make_shared<std::promise<bool>>();
                auto future = promise->get_future();

                auto* taskInterface = SKSE::GetTaskInterface();
                if (!taskInterface) break;

                taskInterface->AddTask([promise]() {
                    if (SkyPotion::g_isShuttingDown.load() || !Util::IsGameSessionReady()) {
                        promise->set_value(false);
                        return;
                    }

                    AlchemyController::DismissOpenMessageBoxes();

                    auto* menu = AlchemyController::GetAlchemyMenu();
                    if (!menu || menu->selectedIndexes.size() < 2) {
                        promise->set_value(false); // Out of ingredients or menu closed
                        return;
                    }
                    auto* userEvents = RE::UserEvents::GetSingleton();
                    if (!userEvents) {
                        promise->set_value(false);
                        return;
                    }

                    menu->ProcessUserEvent(&userEvents->xButton);
                    AlchemyController::DismissOpenMessageBoxes();
                    promise->set_value(true);
                });

                if (future.wait_for(std::chrono::seconds(2)) == std::future_status::ready) {
                    if (!future.get()) {
                        SKSE::log::info("CraftPotions: Finished at potion {} (ingredients depleted or menu closed)", crafted);
                        break;
                    }
                    crafted++;
                } else {
                    SKSE::log::warn("CraftPotions: Timeout waiting for craft task");
                    break;
                }

                // 200ms delay between crafts for UI/inventory/sound settling
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
            }

            s_isCrafting.store(false);
            SKSE::log::info("CraftPotions: Bulk craft completed (total crafted: {})", crafted);

            if (!SkyPotion::g_isShuttingDown.load()) {
                auto* taskInterface = SKSE::GetTaskInterface();
                if (taskInterface) {
                    taskInterface->AddTask([]() {
                        if (!SkyPotion::g_isShuttingDown.load()) {
                            AlchemyController::DismissOpenMessageBoxes();
                        }
                    });
                }
            }
        }).detach();

        result.success = true;
        result.crafted = true;
        result.craftedCount = count;
        result.message = std::format("Started bulk craft of up to {} potions", count);
        return result;
    }

    AlchemySelectResult AlchemyController::SelectIngredients(
        const std::vector<std::string>& formIdStrings,
        bool autoCraft,
        int craftCount
    ) {
        std::vector<RE::FormID> formIds;
        formIds.reserve(formIdStrings.size());

        for (const auto& str : formIdStrings) {
            try {
                uint64_t val = std::stoull(str, nullptr, 0);
                RE::FormID id = static_cast<RE::FormID>(val & 0xFFFFFFFFULL);
                formIds.push_back(id);
            } catch (const std::exception& e) {
                SKSE::log::warn("Invalid FormID string '{}': {}", str, e.what());
            }
        }

        return SelectIngredients(formIds, autoCraft, craftCount);
    }

    AlchemySelectResult AlchemyController::SelectIngredients(
        const std::vector<RE::FormID>& formIds,
        bool autoCraft,
        int craftCount
    ) {
        AlchemySelectResult result;

        auto* alchemyMenu = GetAlchemyMenu();
        if (!alchemyMenu) {
            SKSE::log::warn("SelectIngredients: Alchemy workbench is not open");
            result.success = false;
            result.message = "Alchemy workbench is not open";
            return result;
        }

        if (!IsAlchemyItemListReady()) {
            SKSE::log::warn("SelectIngredients: Alchemy item list is not ready (categories or transitioning)");
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
            SKSE::log::warn("SelectIngredients: Target item list not found");
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

            if (autoCraft && craftCount > 0 && result.selectedIngredients.size() >= 2) {
                return CraftPotions(craftCount);
            }
            return result;
        }

        // 4. Untoggle items via Scaleform (display index) with FxDelegate fallback
        for (const auto& [remId, remIdx] : toRemove) {
            SKSE::log::info("SelectIngredients: Untoggling 0x{:08X} at cppIndex {}", remId, remIdx);
            if (!ToggleIngredientEntry(alchemyMenu, craftingMenu.get(), targetList, remIdx, remId)) {
                SKSE::log::warn("SelectIngredients: Failed to untoggle 0x{:08X} at cppIndex {}", remId, remIdx);
            }
        }

        // 5. Toggle ON items to add via Scaleform (display index) with FxDelegate fallback
        for (auto addId : toAdd) {
            bool found = false;
            for (std::uint32_t i = 0; i < alchemyMenu->ingredientEntries.size(); ++i) {
                auto& entry = alchemyMenu->ingredientEntries[i];
                if (!entry.ingredient || !entry.ingredient->object) continue;

                if (entry.ingredient->object->GetFormID() == addId) {
                    found = true;
                    SKSE::log::info("SelectIngredients: Toggling ON 0x{:08X} at cppIndex {}", addId, i);
                    if (!ToggleIngredientEntry(alchemyMenu, craftingMenu.get(), targetList, i, addId)) {
                        SKSE::log::warn("SelectIngredients: Failed to toggle ON 0x{:08X} at cppIndex {}", addId, i);
                    }

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
        RefreshAlchemyUI(alchemyMenu, &targetList);

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
            return CraftPotions(craftCount);
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

    void MenuOpenCloseHandler::StartMonitoring() {
        if (!Server::WebSocketServer::GetSingleton().HasConnectedClients()) {
            return;
        }

        std::shared_ptr<MonitorSession> session;
        {
            std::lock_guard<std::mutex> lock(s_sessionMutex);
            if (s_monitorSession && s_monitorSession->active.load()) {
                return; // Already monitoring
            }
            if (s_monitorSession) {
                s_monitorSession->active.store(false);
            }
            s_monitorSession = std::make_shared<MonitorSession>();
            session = s_monitorSession;
        }

        std::thread([session]() {
            while (session->active.load() && !SkyPotion::g_isShuttingDown.load()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(250));

                if (!session->active.load() || SkyPotion::g_isShuttingDown.load() ||
                    !Server::WebSocketServer::GetSingleton().HasConnectedClients()) {
                    break;
                }

                auto* taskInterface = SKSE::GetTaskInterface();
                if (!taskInterface) {
                    break;
                }

                auto promise = std::make_shared<std::promise<bool>>();
                auto future = promise->get_future();

                taskInterface->AddTask([session, promise]() {
                    if (!session->active.load() || SkyPotion::g_isShuttingDown.load() || !Util::IsGameSessionReady() ||
                        !Server::WebSocketServer::GetSingleton().HasConnectedClients()) {
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

                        Server::WebSocketServer::GetSingleton().BroadcastBinary(
                            Sync::ProtoPacketBuilder::BuildAlchemyBenchState(true, session->lastSelected)
                        );
                    } else if (!isReady && session->wasListReady) {
                        session->wasListReady = false;
                        session->lastSelected.clear();
                        SKSE::log::info("CraftingMenu: Alchemy ItemList CLOSED (back to categories).");

                        Server::WebSocketServer::GetSingleton().BroadcastBinary(
                            Sync::ProtoPacketBuilder::BuildAlchemyBenchState(false, {})
                        );
                    } else if (isReady && session->wasListReady) {
                        auto currentSelected = AlchemyController::GetSelectedIngredientFormIDs();
                        if (currentSelected != session->lastSelected) {
                            session->lastSelected = currentSelected;
                            SKSE::log::info("CraftingMenu: Alchemy selection changed in Skyrim (count={})",
                                session->lastSelected.size());

                            Server::WebSocketServer::GetSingleton().BroadcastBinary(
                                Sync::ProtoPacketBuilder::BuildAlchemyBenchState(true, session->lastSelected)
                            );
                        }
                    }

                    promise->set_value(true);
                });

                // Wait for task completion. If main thread is paused (Alt-Tab, System Menu, etc.),
                // keep waiting without killing the monitor thread or accumulating duplicate tasks.
                bool taskDone = false;
                while (session->active.load() && !SkyPotion::g_isShuttingDown.load() &&
                       Server::WebSocketServer::GetSingleton().HasConnectedClients()) {
                    if (future.wait_for(std::chrono::milliseconds(250)) == std::future_status::ready) {
                        taskDone = true;
                        if (!future.get()) {
                            // Menu closed or invalid state, exit cleanly
                            session->active.store(false);
                            return;
                        }
                        break;
                    }
                }

                if (!taskDone) {
                    break; // Shutting down, session cancelled, or client disconnected
                }
            }

            session->active.store(false);
        }).detach();
    }

    void MenuOpenCloseHandler::StopMonitoring() {
        std::lock_guard<std::mutex> lock(s_sessionMutex);
        if (s_monitorSession) {
            s_monitorSession->active.store(false);
            s_monitorSession.reset();
        }
    }

    bool MenuOpenCloseHandler::IsMonitoring() {
        std::lock_guard<std::mutex> lock(s_sessionMutex);
        return s_monitorSession && s_monitorSession->active.load();
    }

    RE::BSEventNotifyControl MenuOpenCloseHandler::ProcessEvent(
        const RE::MenuOpenCloseEvent* a_event,
        RE::BSTEventSource<RE::MenuOpenCloseEvent>*) {
        if (!a_event) return RE::BSEventNotifyControl::kContinue;

        // If no clients are connected to WebSocket, do absolutely nothing!
        if (!Server::WebSocketServer::GetSingleton().HasConnectedClients()) {
            return RE::BSEventNotifyControl::kContinue;
        }

        if (a_event->menuName == RE::MessageBoxMenu::MENU_NAME && a_event->opening) {
            auto* taskInterface = SKSE::GetTaskInterface();
            if (taskInterface) {
                taskInterface->AddTask([]() {
                    if (AlchemyController::IsAlchemyMenuOpen()) {
                        SKSE::log::info("MenuOpenCloseEvent: MessageBoxMenu opened at alchemy workbench. Auto-dismissing.");
                        AlchemyController::DismissOpenMessageBoxes();
                    }
                });
            }
            return RE::BSEventNotifyControl::kContinue;
        }

        if (a_event->menuName == RE::MainMenu::MENU_NAME && a_event->opening) {
            SKSE::log::info("MenuOpenCloseEvent: MainMenu opening. Stopping craft and monitor session.");
            AlchemyController::StopCrafting();
            StopMonitoring();

            Server::WebSocketServer::GetSingleton().BroadcastBinary(
                Sync::ProtoPacketBuilder::BuildAlchemyBenchState(false, {})
            );
            return RE::BSEventNotifyControl::kContinue;
        }

        if (a_event->menuName == RE::LoadingMenu::MENU_NAME) {
            if (a_event->opening) {
                SKSE::log::info("MenuOpenCloseEvent: LoadingMenu opening. Stopping craft and monitor session.");
                AlchemyController::StopCrafting();
                StopMonitoring();

                Server::WebSocketServer::GetSingleton().BroadcastBinary(
                    Sync::ProtoPacketBuilder::BuildAlchemyBenchState(false, {})
                );
            } else {
                SKSE::log::info("MenuOpenCloseEvent: LoadingMenu closed. Scheduling session snapshot.");
                Server::WebSocketServer::GetSingleton().ScheduleSessionSnapshot();
            }
            return RE::BSEventNotifyControl::kContinue;
        }

        if (a_event->menuName == RE::CraftingMenu::MENU_NAME) {
            bool opening = a_event->opening;
            SKSE::log::info("MenuOpenCloseEvent: CraftingMenu opening={}", opening);

            if (opening) {
                StartMonitoring();
            } else {
                AlchemyController::StopCrafting();
                StopMonitoring();

                Server::WebSocketServer::GetSingleton().BroadcastBinary(
                    Sync::ProtoPacketBuilder::BuildAlchemyBenchState(false, {})
                );
            }
            return RE::BSEventNotifyControl::kContinue;
        }

        // 퍽/스킬 레벨업 메뉴 닫힐 때 연금술 스탯 즉시 동기화
        if (a_event->menuName == RE::StatsMenu::MENU_NAME && !a_event->opening) {
            InventoryEventHandler::GetSingleton()->RequestAlchemyStatsSync();
        }

        return RE::BSEventNotifyControl::kContinue;
    }

}
