#pragma once

void showPokedex(Bot& bot, int64_t chatId, int64_t uid, int filterIdx = 0, int page = 0) {
    vector<string> filters = {"Все", "Секретная", "Мифическая", "Легендарная", "Эпическая", "Редкая", "Обычная"};
    if (filterIdx < 0 || filterIdx >= (int)filters.size()) filterIdx = 0;
    string curFilter = filters[filterIdx];

    // Загружаем карты игрока: card_id -> awake (0 или 1)
    map<int, int> owned;
    int primeCnt = 0;
    sqlite3_stmt* st;
    if (sqlite3_prepare_v2(db, ("SELECT card_id, awake FROM collection WHERE user_id = " + to_string(uid) + ";").c_str(), -1, &st, nullptr) == SQLITE_OK) {
        while (sqlite3_step(st) == SQLITE_ROW) {
            int cid = sqlite3_column_int(st, 0);
            int awk = sqlite3_column_int(st, 1);
            owned[cid] = awk;
            if (awk == 1) primeCnt++;
        }
        sqlite3_finalize(st);
    }

    vector<Card> list;
    for (const auto& c : CARDS_DB) {
        if (curFilter == "Все" || c.rarity == curFilter) list.push_back(c);
    }
    sort(list.begin(), list.end(), [](const Card& a, const Card& b) {
        if (a.power != b.power) return a.power > b.power;
        return a.id < b.id;
    });

    int perPage = 10;
    int maxPage = max(0, ((int)list.size() - 1) / perPage);
    if (page < 0) page = 0;
    if (page > maxPage) page = maxPage;

    string txt = "<b>ЭНЦИКЛОПЕДИЯ ПЕРСОНАЖЕЙ</b>\n"
                 "Открыто у тебя: <b>" + to_string(owned.size()) + " / " + to_string(CARDS_DB.size()) + "</b> (Прайм: <b>" + to_string(primeCnt) + "</b>)\n"
                 "Раздел: <b>" + curFilter + "</b> (Стр. " + to_string(page + 1) + "/" + to_string(maxPage + 1) + ")\n"
                 "<i>[Прайм] = есть Прайм | [+] = есть | [-] = нет</i>\n\n";

    int start = page * perPage;
    int end = min((int)list.size(), start + perPage);
    for (int i = start; i < end; ++i) {
        const Card& c = list[i];
        string mark = "[-]";
        if (owned.count(c.id)) mark = (owned[c.id] == 1 ? "[Прайм]" : "[+]");
        int primePow = static_cast<int>(c.power * 1.35);
        txt += mark + " <b>" + c.name + "</b> [" + c.rarity + "]\n"
               "   Сила: " + to_string(c.power) + " -> Прайм: <b>" + to_string(primePow) + "</b>\n";
    }

    string cid = to_string(chatId);
    auto kb = make_shared<InlineKeyboardMarkup>();

    // Кнопки фильтров по редкости (2 ряда)
    vector<InlineKeyboardButton::Ptr> r1, r2;
    vector<string> shortNames = {"Все", "Секрет", "Мифик", "Лега", "Эпик", "Редкая", "Обычная"};
    for (int f = 0; f < (int)filters.size(); ++f) {
        auto b = make_shared<InlineKeyboardButton>();
        b->text = (f == filterIdx ? "[" + shortNames[f] + "]" : shortNames[f]);
        b->callbackData = "dex_" + cid + "_" + to_string(f) + "_0";
        if (f < 4) r1.push_back(b); else r2.push_back(b);
    }
    kb->inlineKeyboard.push_back(r1);
    kb->inlineKeyboard.push_back(r2);

    // Навигация по страницам
    vector<InlineKeyboardButton::Ptr> nav;
    if (page > 0) {
        auto bp = make_shared<InlineKeyboardButton>(); bp->text = "< Назад";
        bp->callbackData = "dex_" + cid + "_" + to_string(filterIdx) + "_" + to_string(page - 1);
        nav.push_back(bp);
    }
    if (page < maxPage) {
        auto bn = make_shared<InlineKeyboardButton>(); bn->text = "Вперёд >";
        bn->callbackData = "dex_" + cid + "_" + to_string(filterIdx) + "_" + to_string(page + 1);
        nav.push_back(bn);
    }
    if (!nav.empty()) kb->inlineKeyboard.push_back(nav);

    auto closeB = make_shared<InlineKeyboardButton>(); closeB->text = "Закрыть"; closeB->callbackData = "mclose_" + cid;
    kb->inlineKeyboard.push_back({closeB});

    sendUiMsg(bot, chatId, uid, txt, kb);
}

bool handlePokedexCallback(Bot& bot, CallbackQuery::Ptr q, const string& data, int64_t cid) {
    if (data.rfind("dex_", 0) == 0) {
        string r = data.substr(4);
        size_t s1 = r.find('_'), s2 = r.find('_', s1 + 1);
        if (s1 != string::npos && s2 != string::npos) {
            int64_t chatId = stoll(r.substr(0, s1));
            int fIdx = stoi(r.substr(s1 + 1, s2 - s1 - 1));
            int pg = stoi(r.substr(s2 + 1));
            AnswerCallbackQueryArgs a; a.callbackQueryId = q->id;
            try { bot.getApi().answerCallbackQuery(a); } catch (...) {}
            showPokedex(bot, chatId, cid, fIdx, pg);
        }
        return true;
    }
    return false;
}
