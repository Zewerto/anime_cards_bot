#pragma once
int getCoins(int64_t uid);
void changeCoins(int64_t uid, int delta);

map<pair<int64_t, int64_t>, int32_t> LAST_UI_MSG;

void delLastUi(Bot& bot, int64_t chatId, int64_t uid) {
    auto k = make_pair(chatId, uid);
    if (LAST_UI_MSG.count(k) && LAST_UI_MSG[k] != 0) {
        try {
            DeleteMessageArgs d; d.chatId = chatId; d.messageId = LAST_UI_MSG[k];
            bot.getApi().deleteMessage(d);
        } catch (...) {}
        LAST_UI_MSG.erase(k);
    }
}

void sendUiMsg(Bot& bot, int64_t chatId, int64_t uid, const string& text, InlineKeyboardMarkup::Ptr kb = nullptr) {
    delLastUi(bot, chatId, uid);
    SendMessageArgs a; a.chatId = chatId; a.text = text; a.parseMode = "HTML";
    if (LAST_THREAD.count(chatId) && LAST_THREAD[chatId] != 0) a.messageThreadId = LAST_THREAD[chatId];
    if (kb) a.replyMarkup = kb;
    try {
        auto m = bot.getApi().sendMessage(a);
        if (m) LAST_UI_MSG[{chatId, uid}] = m->messageId;
    } catch (...) {}
}

void initMarketDB() {
    sqlite3_exec(db, "ALTER TABLE users ADD COLUMN max_rank INTEGER DEFAULT 0;", nullptr, nullptr, nullptr);
    sqlite3_exec(db, "CREATE TABLE IF NOT EXISTS auction (id INTEGER PRIMARY KEY AUTOINCREMENT, seller_id INTEGER, seller_name TEXT, card_id INTEGER, awake INTEGER, price INTEGER);", nullptr, nullptr, nullptr);
}

int getRankTier(int pts) {
    if (pts < 300) return 0;
    if (pts < 800) return 1;
    if (pts < 1600) return 2;
    if (pts < 2800) return 3;
    if (pts < 4500) return 4;
    return 5;
}

string checkRankUpReward(int64_t uid) {
    int pts = getUserPoints(uid);
    int curTier = getRankTier(pts);
    int maxTier = 0;
    sqlite3_stmt* st;
    if (sqlite3_prepare_v2(db, ("SELECT max_rank FROM users WHERE user_id = " + to_string(uid) + ";").c_str(), -1, &st, nullptr) == SQLITE_OK) {
        if (sqlite3_step(st) == SQLITE_ROW) maxTier = sqlite3_column_int(st, 0);
        sqlite3_finalize(st);
    }
    if (curTier <= maxTier) return "";

    vector<int> rewards = {0, 300, 700, 1500, 3000, 6000};
    vector<string> names = {"Бронза", "Серебро", "Золото", "Платина", "Алмаз", "Бриллиант"};
    int totalBonus = 0;
    for (int t = maxTier + 1; t <= curTier; ++t) totalBonus += rewards[t];

    sqlite3_exec(db, ("UPDATE users SET max_rank = " + to_string(curTier) + " WHERE user_id = " + to_string(uid) + ";").c_str(), nullptr, nullptr, nullptr);
    changeCoins(uid, totalBonus);
    return "\n🎉 <b>НОВЫЙ РАНГ: " + names[curTier] + "!</b> Награда: <b>+" + to_string(totalBonus) + " монет!</b>";
}

int getBotSellPrice(const Card& c) {
    int base = 50;
    if (c.rarity == "Редкая") base = 120;
    else if (c.rarity == "Эпическая") base = 250;
    else if (c.rarity == "Легендарная") base = 600;
    else if (c.rarity == "Мифическая") base = 1500;
    else if (c.rarity == "Секретная") base = 4000;
    return c.awake ? static_cast<int>(base * 1.5) : base;
}

void showAuction(Bot& bot, int64_t chatId, int64_t uid, int page = 0) {
    struct Lot { int id; int64_t sellerId; string sellerName; int cardId; int awake; int price; };
    vector<Lot> lots;
    sqlite3_stmt* st;
    if (sqlite3_prepare_v2(db, "SELECT id, seller_id, seller_name, card_id, awake, price FROM auction ORDER BY id DESC;", -1, &st, nullptr) == SQLITE_OK) {
        while (sqlite3_step(st) == SQLITE_ROW) {
            Lot l;
            l.id = sqlite3_column_int(st, 0);
            l.sellerId = sqlite3_column_int64(st, 1);
            const unsigned char* sn = sqlite3_column_text(st, 2);
            l.sellerName = sn ? reinterpret_cast<const char*>(sn) : "Игрок";
            l.cardId = sqlite3_column_int(st, 3);
            l.awake = sqlite3_column_int(st, 4);
            l.price = sqlite3_column_int(st, 5);
            lots.push_back(l);
        }
        sqlite3_finalize(st);
    }

    auto kb = make_shared<InlineKeyboardMarkup>();
    string cid = to_string(chatId);
    auto sellBtn = make_shared<InlineKeyboardButton>();
    sellBtn->text = "+ Продать своего героя";
    sellBtn->callbackData = "msell_" + cid + "_0";
    kb->inlineKeyboard.push_back({sellBtn});

    string txt = "<b>АУКЦИОН ИГРОКОВ</b> (Баланс: <b>" + to_string(getCoins(uid)) + " монет</b>)\n\n";
    if (lots.empty()) {
        txt += "На аукционе пока пусто. Выстави своего героя первым!";
    } else {
        int perPage = 5;
        int maxPage = max(0, ((int)lots.size() - 1) / perPage);
        if (page > maxPage) page = maxPage;
        int start = page * perPage;
        int end = min((int)lots.size(), start + perPage);

        for (int i = start; i < end; ++i) {
            Lot& l = lots[i];
            Card c = getCard(l.cardId);
            string cTitle = (l.awake ? "[Прайм] " : "") + c.name;
            int pwr = l.awake ? static_cast<int>(c.power * 1.35) : c.power;
            txt += "#" + to_string(l.id) + " <b>" + cTitle + "</b> [" + c.rarity + ", " + to_string(pwr) + "] — <b>" + to_string(l.price) + " монет</b> (" + l.sellerName + ")\n";

            auto b = make_shared<InlineKeyboardButton>();
            if (l.sellerId == uid) {
                b->text = "Снять лот #" + to_string(l.id) + " (" + cTitle + ")";
                b->callbackData = "mcancel_" + cid + "_" + to_string(l.id);
            } else {
                b->text = "Купить #" + to_string(l.id) + " за " + to_string(l.price) + " монет";
                b->callbackData = "mbuy_" + cid + "_" + to_string(l.id);
            }
            kb->inlineKeyboard.push_back({b});
        }

        vector<InlineKeyboardButton::Ptr> nav;
        if (page > 0) {
            auto p = make_shared<InlineKeyboardButton>(); p->text = "< Назад"; p->callbackData = "mauc_" + cid + "_" + to_string(page - 1);
            nav.push_back(p);
        }
        if (page < maxPage) {
            auto n = make_shared<InlineKeyboardButton>(); n->text = "Вперёд >"; n->callbackData = "mauc_" + cid + "_" + to_string(page + 1);
            nav.push_back(n);
        }
        if (!nav.empty()) kb->inlineKeyboard.push_back(nav);
    }

    auto closeB = make_shared<InlineKeyboardButton>(); closeB->text = "Закрыть"; closeB->callbackData = "mclose_" + cid;
    kb->inlineKeyboard.push_back({closeB});
    sendUiMsg(bot, chatId, uid, txt, kb);
}

void showSellList(Bot& bot, int64_t chatId, int64_t uid, int page = 0) {
    auto cards = getUserCards(uid);
    auto kb = make_shared<InlineKeyboardMarkup>();
    string cid = to_string(chatId);

    if (cards.empty()) {
        auto back = make_shared<InlineKeyboardButton>(); back->text = "< На аукцион"; back->callbackData = "mauc_" + cid + "_0";
        kb->inlineKeyboard.push_back({back});
        sendUiMsg(bot, chatId, uid, "У тебя нет карточек для продажи!", kb);
        return;
    }

    int perPage = 6;
    int maxPage = max(0, ((int)cards.size() - 1) / perPage);
    if (page > maxPage) page = maxPage;
    int start = page * perPage;
    int end = min((int)cards.size(), start + perPage);

    for (int i = start; i < end; ++i) {
        Card c = cards[i].first;
        auto b = make_shared<InlineKeyboardButton>();
        b->text = c.name + " [" + c.rarity + "] (Боту: " + to_string(getBotSellPrice(c)) + ")";
        b->callbackData = "mpick_" + cid + "_" + to_string(c.id);
        kb->inlineKeyboard.push_back({b});
    }

    vector<InlineKeyboardButton::Ptr> nav;
    if (page > 0) { auto p = make_shared<InlineKeyboardButton>(); p->text = "< Пред."; p->callbackData = "msell_" + cid + "_" + to_string(page - 1); nav.push_back(p); }
    if (page < maxPage) { auto n = make_shared<InlineKeyboardButton>(); n->text = "След. >"; n->callbackData = "msell_" + cid + "_" + to_string(page + 1); nav.push_back(n); }
    if (!nav.empty()) kb->inlineKeyboard.push_back(nav);

    auto back = make_shared<InlineKeyboardButton>(); back->text = "< Назад на аукцион"; back->callbackData = "mauc_" + cid + "_0";
    kb->inlineKeyboard.push_back({back});
    sendUiMsg(bot, chatId, uid, "<b>ПРОДАЖА ПЕРСОНАЖА</b>\nВыбери героя, которого хочешь продать боту или выставить на аукцион:", kb);
}

bool handleMarketCallback(Bot& bot, CallbackQuery::Ptr q, const string& data, int64_t cid, const string& uname) {
    auto alert = [&](const string& msg) { AnswerCallbackQueryArgs a; a.callbackQueryId = q->id; a.text = msg; a.showAlert = true; try { bot.getApi().answerCallbackQuery(a); } catch (...) {} };
    auto ok = [&]() { AnswerCallbackQueryArgs a; a.callbackQueryId = q->id; try { bot.getApi().answerCallbackQuery(a); } catch (...) {} };

    if (data.rfind("mclose_", 0) == 0) {
        delLastUi(bot, stoll(data.substr(7)), cid); ok(); return true;
    }
    if (data.rfind("mauc_", 0) == 0) {
        string r = data.substr(5); size_t sp = r.find('_');
        ok(); showAuction(bot, stoll(r.substr(0, sp)), cid, stoi(r.substr(sp + 1))); return true;
    }
    if (data.rfind("msell_", 0) == 0) {
        string r = data.substr(6); size_t sp = r.find('_');
        ok(); showSellList(bot, stoll(r.substr(0, sp)), cid, stoi(r.substr(sp + 1))); return true;
    }
    if (data.rfind("mpick_", 0) == 0) {
        string r = data.substr(6); size_t sp = r.find('_');
        int64_t chatId = stoll(r.substr(0, sp)); int cardId = stoi(r.substr(sp + 1));
        Card chosen = getCard(cardId); bool has = false;
        for (const auto& p : getUserCards(cid)) if (p.first.id == cardId) { chosen = p.first; has = true; }
        if (!has) { alert("Эта карта уже не у тебя!"); return true; }

        int botPrice = getBotSellPrice(chosen);
        auto kb = make_shared<InlineKeyboardMarkup>();
        string scid = to_string(chatId), scard = to_string(cardId);
        auto bBot = make_shared<InlineKeyboardButton>();
        bBot->text = "Продать боту сразу (+" + to_string(botPrice) + " монет)";
        bBot->callbackData = "msbot_" + scid + "_" + scard;
        kb->inlineKeyboard.push_back({bBot});

        vector<int> prices = {200, 500, 1000, 2500, 5000, 10000};
        vector<InlineKeyboardButton::Ptr> row1, row2;
        for (int i = 0; i < 6; ++i) {
            auto bp = make_shared<InlineKeyboardButton>();
            bp->text = "Лот: " + to_string(prices[i]) + " монет";
            bp->callbackData = "msauc_" + scid + "_" + scard + "_" + to_string(prices[i]);
            if (i < 3) row1.push_back(bp); else row2.push_back(bp);
        }
        kb->inlineKeyboard.push_back(row1);
        kb->inlineKeyboard.push_back(row2);

        auto back = make_shared<InlineKeyboardButton>(); back->text = "< Назад"; back->callbackData = "msell_" + scid + "_0";
        kb->inlineKeyboard.push_back({back});
        ok();
        sendUiMsg(bot, chatId, cid, "Выбран герой: <b>" + chosen.name + "</b> [" + chosen.rarity + ", Сила: " + to_string(chosen.power) + "]\n\nКак хочешь его продать?", kb);
        return true;
    }
    if (data.rfind("msbot_", 0) == 0) {
        string r = data.substr(6); size_t sp = r.find('_');
        int64_t chatId = stoll(r.substr(0, sp)); int cardId = stoi(r.substr(sp + 1));
        Card chosen = getCard(cardId); bool has = false;
        for (const auto& p : getUserCards(cid)) if (p.first.id == cardId) { chosen = p.first; has = true; }
        if (!has) { alert("Карта не найдена!"); return true; }
        int price = getBotSellPrice(chosen);
        sqlite3_exec(db, ("DELETE FROM collection WHERE user_id = " + to_string(cid) + " AND card_id = " + to_string(cardId) + ";").c_str(), nullptr, nullptr, nullptr);
        changeCoins(cid, price);
        ok();
        sendUiMsg(bot, chatId, cid, "Ты продал героя <b>" + chosen.name + "</b> боту за <b>+" + to_string(price) + " монет</b>!\nТекущий баланс: <b>" + to_string(getCoins(cid)) + " монет</b>.", menuKb(chatId));
        return true;
    }
    if (data.rfind("msauc_", 0) == 0) {
        string r = data.substr(6); size_t s1 = r.find('_'), s2 = r.find('_', s1 + 1);
        int64_t chatId = stoll(r.substr(0, s1)); int cardId = stoi(r.substr(s1 + 1, s2 - s1 - 1)); int price = stoi(r.substr(s2 + 1));
        Card chosen = getCard(cardId); bool has = false;
        for (const auto& p : getUserCards(cid)) if (p.first.id == cardId) { chosen = p.first; has = true; }
        if (!has) { alert("Карта не найдена!"); return true; }
        int awk = chosen.awake ? 1 : 0;
        sqlite3_exec(db, ("DELETE FROM collection WHERE user_id = " + to_string(cid) + " AND card_id = " + to_string(cardId) + ";").c_str(), nullptr, nullptr, nullptr);
        sqlite3_exec(db, ("INSERT INTO auction (seller_id, seller_name, card_id, awake, price) VALUES (" + to_string(cid) + ", '" + esc(uname) + "', " + to_string(cardId) + ", " + to_string(awk) + ", " + to_string(price) + ");").c_str(), nullptr, nullptr, nullptr);
        ok();
        showAuction(bot, chatId, cid, 0);
        return true;
    }
    if (data.rfind("mcancel_", 0) == 0) {
        string r = data.substr(8); size_t sp = r.find('_');
        int64_t chatId = stoll(r.substr(0, sp)); int lotId = stoi(r.substr(sp + 1));
        sqlite3_stmt* st; int cardId = 0, awk = 0; int64_t sid = 0;
        if (sqlite3_prepare_v2(db, ("SELECT seller_id, card_id, awake FROM auction WHERE id = " + to_string(lotId) + ";").c_str(), -1, &st, nullptr) == SQLITE_OK) {
            if (sqlite3_step(st) == SQLITE_ROW) { sid = sqlite3_column_int64(st, 0); cardId = sqlite3_column_int(st, 1); awk = sqlite3_column_int(st, 2); }
            sqlite3_finalize(st);
        }
        if (sid != cid || cardId == 0) { alert("Лот уже недоступен."); return true; }
        sqlite3_exec(db, ("DELETE FROM auction WHERE id = " + to_string(lotId) + ";").c_str(), nullptr, nullptr, nullptr);
        sqlite3_exec(db, ("INSERT INTO collection (user_id, card_id, count, awake) VALUES (" + to_string(cid) + ", " + to_string(cardId) + ", 1, " + to_string(awk) + ") ON CONFLICT(user_id, card_id) DO UPDATE SET awake = 1;").c_str(), nullptr, nullptr, nullptr);
        ok();
        showAuction(bot, chatId, cid, 0);
        return true;
    }
    if (data.rfind("mbuy_", 0) == 0) {
        string r = data.substr(5); size_t sp = r.find('_');
        int64_t chatId = stoll(r.substr(0, sp)); int lotId = stoi(r.substr(sp + 1));
        sqlite3_stmt* st; int cardId = 0, awk = 0, price = 0; int64_t sid = 0; string sname;
        if (sqlite3_prepare_v2(db, ("SELECT seller_id, seller_name, card_id, awake, price FROM auction WHERE id = " + to_string(lotId) + ";").c_str(), -1, &st, nullptr) == SQLITE_OK) {
            if (sqlite3_step(st) == SQLITE_ROW) {
                sid = sqlite3_column_int64(st, 0);
                const unsigned char* sn = sqlite3_column_text(st, 1); sname = sn ? reinterpret_cast<const char*>(sn) : "Игрок";
                cardId = sqlite3_column_int(st, 2); awk = sqlite3_column_int(st, 3); price = sqlite3_column_int(st, 4);
            }
            sqlite3_finalize(st);
        }
        if (cardId == 0) { alert("Лот уже куплен или снят!"); return true; }
        if (sid == cid) { alert("Нельзя купить свой же лот!"); return true; }
        if (getCoins(cid) < price) { alert("Недостаточно монет! Нужно " + to_string(price)); return true; }

        changeCoins(cid, -price);
        changeCoins(sid, price);
        sqlite3_exec(db, ("DELETE FROM auction WHERE id = " + to_string(lotId) + ";").c_str(), nullptr, nullptr, nullptr);
        sqlite3_exec(db, ("INSERT INTO collection (user_id, card_id, count, awake) VALUES (" + to_string(cid) + ", " + to_string(cardId) + ", 1, " + to_string(awk) + ") ON CONFLICT(user_id, card_id) DO UPDATE SET awake = 1;").c_str(), nullptr, nullptr, nullptr);
        ok();
        Card c = getCard(cardId);
        sendUiMsg(bot, chatId, cid, "<b>ПОКУПКА НА АУКЦИОНЕ!</b>\n\nИгрок <b>" + uname + "</b> купил <b>" + (awk ? "[Прайм] " : "") + c.name + "</b> у <b>" + sname + "</b> за <b>" + to_string(price) + " монет</b>!", menuKb(chatId));
        return true;
    }
    return false;
}
