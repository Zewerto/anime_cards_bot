#pragma once
struct Skill { string name; int damage; };

vector<Skill> getCardSkills(const Card& c) {
    int cnt = 1;
    if (c.rarity == "Редкая") cnt = 2;
    else if (c.rarity == "Эпическая") cnt = 3;
    else if (c.rarity == "Легендарная") cnt = 4;
    else if (c.rarity == "Мифическая") cnt = 5;
    else if (c.rarity == "Секретная") cnt = 6;

    vector<string> pool = {
        "Сокрушительный выпад", "Вихрь клинков", "Всплеск чакры",
        "Теневой рывок", "Ярость берсерка", "Алый разрез",
        "Удар небесного дракона", "Прорыв барьера", "Громовой раскат",
        "Печать пустоты", "Абсолютный ноль", "Судный день"
    };
    vector<double> mults = {2.65};
    if (cnt == 2) mults = {2.2, 2.8};
    else if (cnt == 3) mults = {2.0, 2.5, 3.1};
    else if (cnt == 4) mults = {1.9, 2.4, 2.9, 3.5};
    else if (cnt == 5) mults = {1.8, 2.3, 2.8, 3.4, 4.0};
    else if (cnt == 6) mults = {2.0, 2.5, 3.0, 3.6, 4.2, 5.0};

    vector<Skill> res;
    for (int i = 0; i < cnt; ++i) {
        string sName = pool[(c.id * 7 + i * 3) % pool.size()];
        res.push_back({sName, static_cast<int>(c.power * mults[i])});
    }
    return res;
}
