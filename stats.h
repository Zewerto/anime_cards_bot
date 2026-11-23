#pragma once

void showBotStats(Bot& bot, int64_t chatId, int64_t uid, int mode = 0) {
    struct StatRow {
        int64_t id;
        string name;
        int coins;
        int power;
        int cardCnt;
    };

    vector<StatRow> players;
    int totalUsers = 0, totalCards = 0, totalPrimes = 0;

    sqlite3_stmt* st;
    if (sqlite3_prepare_v2(db, "SELECT user_id, username, coins FROM users;", -1, &st, nullptr) == SQLITE_OK) {
        while (sqlite3_step(st) == SQLITE_ROW) {
            totalUsers++;
            StatRow r;
            r.id = sqlite3_column_int64(st, 0);
            const unsigned char* u = sqlite3_column_text(st, 1);
            r.name = u ? reinterpret_cast<const char*>(u) : "";
            if (r.name.empty()) r.name = "Игрок #" + to_string(r.id % 10000);
            r.coins = sqlite3_column_int(st, 2);
            r.power = 0;
            r.cardCnt = 0;

            auto cards = getUserCards(r.id);
            r.cardCnt = static_cast<int>(cards.size());
            for (const auto& p : cards) {
                r.power += p.first.power;
                if (p.first.awake) totalPrimes++;
            }
            totalCards += r.cardCnt;
            players.push_back(r);
        }
        sqlite3_finalize(st);
    }

    if (mode == 1) {
        sort(players.begin(), players.end(), [](const StatRow& a, const StatRow& b) { return a.coins > b.coins; });
    } else if (mode == 2) {
        sort(players.begin(), players.end(), [](const StatRow& a, const StatRow& b) { return a.cardCnt > b.cardCnt; });
    } else {
        sort(players.begin(), players.end(), [](const StatRow& a, const StatRow& b) { return a.power > b.power; });
    }

    string title = (mode == 1) ? "ТОП-10 ПО МОНЕТАМ" : ((mode == 2) ? "ТОП-10 ПО КОЛЛЕКЦИИ" : "ТОП-10 ПО СИЛЕ КАРТ");
    string txt = "<b>СТАТИСТИКА БОТА</b>\n"
                 "Всего игроков: <b>" + to_string(totalUsers) + "</b>\n"
                 "Выбито карт: <b>" + to_string(totalCards) + "</b> (Прайм: <b>" + to_string(totalPrimes) + "</b>)\n\n"
                 "<b>" + title + ":</b>\n\n";

    int lim = min(10, static_cast<int>(players.size()));
    for (int i = 0; i < lim; ++i) {
        string pos = (i == 0 ? "1." : (i == 1 ? "2." : (i == 2 ? "3." : to_string(i + 1) + ".")));
        string val = (mode == 1) ? (to_string(players[i].coins) + " монет")
                   : ((mode == 2) ? (to_string(players[i].cardCnt) + " / " + to_string(CARDS_DB.size()) + " карт.")
                   : ("Сила: " + to_string(players[i].power)));
        txt += pos + " <b>" + players[i].name + "</b> — <b>" + val + "</b>\n";
    }

    string cid = to_string(chatId);
    auto kb = make_shared<InlineKeyboardMarkup>();
    auto b0 = make_shared<InlineKeyboardButton>(); b0->text = (mode == 0 ? "[Сила]" : "Сила"); b0->callbackData = "st_" + cid + "_0";
    auto b1 = make_shared<InlineKeyboardButton>(); b1->text = (mode == 1 ? "[Монеты]" : "Монеты"); b1->callbackData = "st_" + cid + "_1";
    auto b2 = make_shared<InlineKeyboardButton>(); b2->text = (mode == 2 ? "[Коллекция]" : "Коллекция"); b2->callbackData = "st_" + cid + "_2";
    auto bClose = make_shared<InlineKeyboardButton>(); bClose->text = "Закрыть"; bClose->callbackData = "mclose_" + cid;
    kb->inlineKeyboard.push_back({b0, b1, b2});
    kb->inlineKeyboard.push_back({bClose});

    sendUiMsg(bot, chatId, uid, txt, kb);
}

bool handleStatsCallback(Bot& bot, CallbackQuery::Ptr q, const string& data, int64_t cid) {
    if (data.rfind("st_", 0) == 0) {
        string r = data.substr(3); size_t sp = r.find('_');
        int64_t chatId = stoll(r.substr(0, sp));
        int mode = stoi(r.substr(sp + 1));
        AnswerCallbackQueryArgs a; a.callbackQueryId = q->id;
        try { bot.getApi().answerCallbackQuery(a); } catch (...) {}
        showBotStats(bot, chatId, cid, mode);
        return true;
    }
    return false;
}
