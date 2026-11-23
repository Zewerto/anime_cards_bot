#pragma once

bool isCreator(int64_t uid) {
    int64_t adm = getAdminId();
    return (adm != 0 && uid == adm);
}

void showAdminMain(Bot& bot, int64_t chatId, int64_t uid) {
    if (!isCreator(uid)) return;
    string cid = to_string(chatId);
    auto kb = make_shared<InlineKeyboardMarkup>();

    auto bSelfCoins = make_shared<InlineKeyboardButton>(); bSelfCoins->text = "💰 +5000 монет себе"; bSelfCoins->callbackData = "adm_sc_" + cid;
    auto bSelfPrime = make_shared<InlineKeyboardButton>(); bSelfPrime->text = "⚡ Прайм себе"; bSelfPrime->callbackData = "adm_sp_" + cid;
    kb->inlineKeyboard.push_back({bSelfCoins, bSelfPrime});

    auto bSelfLeg = make_shared<InlineKeyboardButton>(); bSelfLeg->text = "📦 Лег. сундук себе"; bSelfLeg->callbackData = "adm_sl_" + cid;
    auto bSelfSec = make_shared<InlineKeyboardButton>(); bSelfSec->text = "🌌 Секр. сундук себе"; bSelfSec->callbackData = "adm_ss_" + cid;
    kb->inlineKeyboard.push_back({bSelfLeg, bSelfSec});

    auto bUsers = make_shared<InlineKeyboardButton>(); bUsers->text = "🎯 Выбрать игрока (выдача)"; bUsers->callbackData = "adm_ulist_" + cid + "_0";
    kb->inlineKeyboard.push_back({bUsers});

    auto bBoss = make_shared<InlineKeyboardButton>(); bBoss->text = "👹 Сбросить / Новый Босс"; bBoss->callbackData = "adm_boss_" + cid;
    auto bClose = make_shared<InlineKeyboardButton>(); bClose->text = "❌ Закрыть"; bClose->callbackData = "mclose_" + cid;
    kb->inlineKeyboard.push_back({bBoss, bClose});

    string txt = "👑 <b>ПАНЕЛЬ СОЗДАТЕЛЯ</b>\n\n"
                 "Доступна только тебе. Выбери действие для себя или открой список игроков для выдачи наград:";
    sendUiMsg(bot, chatId, uid, txt, kb);
}

void showAdminUserList(Bot& bot, int64_t chatId, int64_t uid, int page = 0) {
    if (!isCreator(uid)) return;
    vector<pair<int64_t, string>> users;
    sqlite3_stmt* st;
    if (sqlite3_prepare_v2(db, "SELECT user_id, username FROM users ORDER BY rowid DESC;", -1, &st, nullptr) == SQLITE_OK) {
        while (sqlite3_step(st) == SQLITE_ROW) {
            int64_t id = sqlite3_column_int64(st, 0);
            const unsigned char* u = sqlite3_column_text(st, 1);
            string nm = u ? reinterpret_cast<const char*>(u) : ("Игрок #" + to_string(id % 10000));
            users.push_back({id, nm});
        }
        sqlite3_finalize(st);
    }

    int perPage = 6;
    int maxPage = max(0, ((int)users.size() - 1) / perPage);
    if (page < 0) page = 0;
    if (page > maxPage) page = maxPage;

    string cid = to_string(chatId);
    auto kb = make_shared<InlineKeyboardMarkup>();
    int start = page * perPage;
    int end = min((int)users.size(), start + perPage);

    for (int i = start; i < end; ++i) {
        auto b = make_shared<InlineKeyboardButton>();
        b->text = users[i].second + " (ID: " + to_string(users[i].first % 10000) + ")";
        b->callbackData = "adm_uact_" + cid + "_" + to_string(users[i].first);
        kb->inlineKeyboard.push_back({b});
    }

    vector<InlineKeyboardButton::Ptr> nav;
    if (page > 0) { auto bp = make_shared<InlineKeyboardButton>(); bp->text = "< Назад"; bp->callbackData = "adm_ulist_" + cid + "_" + to_string(page - 1); nav.push_back(bp); }
    if (page < maxPage) { auto bn = make_shared<InlineKeyboardButton>(); bn->text = "Вперёд >"; bn->callbackData = "adm_ulist_" + cid + "_" + to_string(page + 1); nav.push_back(bn); }
    if (!nav.empty()) kb->inlineKeyboard.push_back(nav);

    auto back = make_shared<InlineKeyboardButton>(); back->text = "< В админ-меню"; back->callbackData = "adm_main_" + cid;
    kb->inlineKeyboard.push_back({back});
    sendUiMsg(bot, chatId, uid, "🎯 <b>ВЫБОР ИГРОКА</b> (Стр. " + to_string(page + 1) + "/" + to_string(maxPage + 1) + "):", kb);
}

void showAdminUserActions(Bot& bot, int64_t chatId, int64_t uid, int64_t targetId) {
    if (!isCreator(uid)) return;
    string tName = "Игрок";
    sqlite3_stmt* st;
    if (sqlite3_prepare_v2(db, ("SELECT username FROM users WHERE user_id = " + to_string(targetId) + ";").c_str(), -1, &st, nullptr) == SQLITE_OK) {
        if (sqlite3_step(st) == SQLITE_ROW) {
            const unsigned char* u = sqlite3_column_text(st, 0);
            if (u) tName = reinterpret_cast<const char*>(u);
        }
        sqlite3_finalize(st);
    }

    string cid = to_string(chatId), tid = to_string(targetId);
    auto kb = make_shared<InlineKeyboardMarkup>();
    auto b1 = make_shared<InlineKeyboardButton>(); b1->text = "💰 +1000 монет"; b1->callbackData = "adm_gc1_" + cid + "_" + tid;
    auto b2 = make_shared<InlineKeyboardButton>(); b2->text = "💰 +5000 монет"; b2->callbackData = "adm_gc5_" + cid + "_" + tid;
    kb->inlineKeyboard.push_back({b1, b2});

    auto b3 = make_shared<InlineKeyboardButton>(); b3->text = "⚡ Выдать Прайм"; b3->callbackData = "adm_gp_" + cid + "_" + tid;
    auto b4 = make_shared<InlineKeyboardButton>(); b4->text = "⏳ Сбросить все КД"; b4->callbackData = "adm_cd_" + cid + "_" + tid;
    kb->inlineKeyboard.push_back({b3, b4});

    auto b5 = make_shared<InlineKeyboardButton>(); b5->text = "📦 Лег. сундук"; b5->callbackData = "adm_gl_" + cid + "_" + tid;
    auto b6 = make_shared<InlineKeyboardButton>(); b6->text = "🌌 Секр. сундук"; b6->callbackData = "adm_gs_" + cid + "_" + tid;
    kb->inlineKeyboard.push_back({b5, b6});

    auto back = make_shared<InlineKeyboardButton>(); back->text = "< К списку игроков"; back->callbackData = "adm_ulist_" + cid + "_0";
    kb->inlineKeyboard.push_back({back});

    string txt = "👤 Управление игроком: <b>" + tName + "</b>\n"
                 "💰 Баланс: <b>" + to_string(getCoins(targetId)) + "</b> | 🏅 Очки ранга: <b>" + to_string(getUserPoints(targetId)) + "</b>\n\n"
                 "Выбери награду для выдачи:";
    sendUiMsg(bot, chatId, uid, txt, kb);
}

void execAdminChest(Bot& bot, int64_t chatId, int64_t targetId, bool isSecret) {
    vector<Card> pool;
    for (const auto& c : CARDS_DB) {
        if (isSecret && c.rarity == "Секретная") pool.push_back(c);
        else if (!isSecret && (c.rarity == "Легендарная" || c.rarity == "Мифическая" || c.rarity == "Секретная")) pool.push_back(c);
    }
    if (pool.empty()) return;
    static mt19937 gen(static_cast<unsigned>(chrono::high_resolution_clock::now().time_since_epoch().count()));
    Card w = pool.at(uniform_int_distribution<>(0, (int)pool.size() - 1)(gen));
    sqlite3_exec(db, ("INSERT INTO collection (user_id, card_id, count, awake) VALUES (" + to_string(targetId) + ", " + to_string(w.id) + ", 1, 0) ON CONFLICT(user_id, card_id) DO UPDATE SET awake = 1;").c_str(), nullptr, nullptr, nullptr);

    string title = isSecret ? "🌌 СЕКРЕТНЫЙ СУНДУК ОТ СОЗДАТЕЛЯ" : "📦 ЛЕГЕНДАРНЫЙ СУНДУК ОТ СОЗДАТЕЛЯ";
    sendCard(bot, chatId, w, "<b>" + title + "</b>\n\nГерой: <b>" + w.name + "</b>\nРедкость: <b>" + w.rarity + "</b>\nСила: <b>" + to_string(w.power) + "</b>", nullptr);
}

bool handleAdminCallback(Bot& bot, CallbackQuery::Ptr q, const string& data, int64_t cid) {
    if (data.rfind("adm_", 0) != 0) return false;
    auto alert = [&](const string& msg) { AnswerCallbackQueryArgs a; a.callbackQueryId = q->id; a.text = msg; a.showAlert = true; try { bot.getApi().answerCallbackQuery(a); } catch (...) {} };
    auto ok = [&]() { AnswerCallbackQueryArgs a; a.callbackQueryId = q->id; try { bot.getApi().answerCallbackQuery(a); } catch (...) {} };

    if (!isCreator(cid)) { alert("Доступно только создателю бота!"); return true; }

    if (data.rfind("adm_main_", 0) == 0) { ok(); showAdminMain(bot, stoll(data.substr(9)), cid); return true; }
    if (data.rfind("adm_ulist_", 0) == 0) {
        string r = data.substr(10); size_t sp = r.find('_');
        ok(); showAdminUserList(bot, stoll(r.substr(0, sp)), cid, stoi(r.substr(sp + 1))); return true;
    }
    if (data.rfind("adm_uact_", 0) == 0) {
        string r = data.substr(9); size_t sp = r.find('_');
        ok(); showAdminUserActions(bot, stoll(r.substr(0, sp)), cid, stoll(r.substr(sp + 1))); return true;
    }
    if (data.rfind("adm_sc_", 0) == 0) { changeCoins(cid, 5000); alert("+5000 монет начислено себе!"); return true; }
    if (data.rfind("adm_sp_", 0) == 0) {
        int bestId = 0; for (const auto& p : getUserCards(cid)) if (!p.first.awake) { bestId = p.first.id; break; }
        if (bestId == 0) { alert("У тебя нет обычных карт для Прайма!"); return true; }
        sqlite3_exec(db, ("UPDATE collection SET awake = 1 WHERE user_id = " + to_string(cid) + " AND card_id = " + to_string(bestId) + ";").c_str(), nullptr, nullptr, nullptr);
        alert("Пробуждён в Прайм: " + getCard(bestId).name); return true;
    }
    if (data.rfind("adm_sl_", 0) == 0) { ok(); execAdminChest(bot, stoll(data.substr(7)), cid, false); return true; }
    if (data.rfind("adm_ss_", 0) == 0) { ok(); execAdminChest(bot, stoll(data.substr(7)), cid, true); return true; }
    if (data.rfind("adm_boss_", 0) == 0) {
        int64_t chatId = stoll(data.substr(9));
        CHAT_BOSS[chatId] = BossState{"Антиспираль (Абсолют Босс)", 500000, 500000, {}, {}, {}};
        alert("Новый Босс (500 000 HP) призван в чат!"); return true;
    }

    // Действия над выбранным игроком
    if (data.rfind("adm_gc1_", 0) == 0 || data.rfind("adm_gc5_", 0) == 0 || data.rfind("adm_gp_", 0) == 0 || data.rfind("adm_cd_", 0) == 0 || data.rfind("adm_gl_", 0) == 0 || data.rfind("adm_gs_", 0) == 0) {
        string act = data.substr(4, data.find('_', 4) - 4);
        string r = data.substr(5 + act.size()); size_t sp = r.find('_');
        int64_t chatId = stoll(r.substr(0, sp)), tid = stoll(r.substr(sp + 1));

        if (act == "gc1") { changeCoins(tid, 1000); alert("Выдано +1000 монет!"); showAdminUserActions(bot, chatId, cid, tid); }
        else if (act == "gc5") { changeCoins(tid, 5000); alert("Выдано +5000 монет!"); showAdminUserActions(bot, chatId, cid, tid); }
        else if (act == "cd") {
            sqlite3_exec(db, ("UPDATE users SET last_roll = 0, exp_end = 0 WHERE user_id = " + to_string(tid) + ";").c_str(), nullptr, nullptr, nullptr);
            if (CHAT_BOSS.count(chatId)) { CHAT_BOSS[chatId].activeFight.erase(tid); CHAT_BOSS[chatId].nextBattleTime.erase(tid); }
            alert("Все таймеры игрока сброшены!"); showAdminUserActions(bot, chatId, cid, tid);
        } else if (act == "gp") {
            int bestId = 0; for (const auto& p : getUserCards(tid)) if (!p.first.awake) { bestId = p.first.id; break; }
            if (bestId == 0) { alert("У игрока нет обычных карт!"); return true; }
            sqlite3_exec(db, ("UPDATE collection SET awake = 1 WHERE user_id = " + to_string(tid) + " AND card_id = " + to_string(bestId) + ";").c_str(), nullptr, nullptr, nullptr);
            alert("Пробуждён в Прайм: " + getCard(bestId).name); showAdminUserActions(bot, chatId, cid, tid);
        } else if (act == "gl") { ok(); execAdminChest(bot, chatId, tid, false); }
        else if (act == "gs") { ok(); execAdminChest(bot, chatId, tid, true); }
        return true;
    }
    return false;
}
