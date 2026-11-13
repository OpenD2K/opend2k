#include "render/ui_text.hpp"

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string_view>
#include <unordered_map>

namespace d2k::render {

namespace {

struct Row {
    const char* en;
    const char* fr;
    const char* de;
    const char* es;
    const char* ru;
};

// Kept short so they fit the buttons and slots.
constexpr Row kRows[] = {
    {"OPTIONS", "OPTIONS", "OPTIONEN", "OPCIONES", "МЕНЮ"},
    {"UPGRADE", "AMÉLIORER", "AUFRÜSTEN", "MEJORAR", "УЛУЧШИТЬ"},
    {"STARPORT", "ASTROPORT", "RAUMHAFEN", "ASTROPUERTO", "КОСМОПОРТ"},
    {"PURCHASE", "ACHETER", "KAUFEN", "COMPRAR", "КУПИТЬ"},
    {"CANCEL", "ANNULER", "ABBRUCH", "CANCELAR", "ОТМЕНА"},
    {"NOTHING TO BUILD", "RIEN À CONSTRUIRE", "NICHTS ZU BAUEN", "NADA QUE CONSTRUIR", "НЕЧЕГО СТРОИТЬ"},
    {"READY", "PRÊT", "BEREIT", "LISTO", "ГОТОВО"},
    {"HOLD", "PAUSE", "HALT", "PAUSA", "ПАУЗА"},
    {"RADAR OFFLINE", "RADAR INACTIF", "RADAR AUS", "RADAR INACTIVO", "РАДАР ОТКЛЮЧЁН"},
    {"UPGRADES", "AMÉLIORATIONS", "AUFRÜSTUNG", "MEJORAS", "УЛУЧШЕНИЯ"},
    {"PALACE", "PALAIS", "PALAST", "PALACIO", "ДВОРЕЦ"},
    {"LEFT", "RESTE", "ÜBRIG", "QUEDAN", "ОСТ."},
    {"ORDER", "COMMANDE", "AUFTRAG", "PEDIDO", "ЗАКАЗ"},
    {"FRIGATE ON ITS WAY", "FRÉGATE EN ROUTE", "FREGATTE UNTERWEGS", "FRAGATA EN CAMINO", "ФРЕГАТ В ПУТИ"},
    {"Buildings", "Bâtiments", "Gebäude", "Edificios", "Здания"},
    {"Infantry", "Infanterie", "Infanterie", "Infantería", "Пехота"},
    {"Vehicles", "Véhicules", "Fahrzeuge", "Vehículos", "Техника"},
    {"Special", "Spécial", "Spezial", "Especial", "Особое"},
    {"Repair", "Réparer", "Reparieren", "Reparar", "Ремонт"},
    {"Sell", "Vendre", "Verkaufen", "Vender", "Продать"},
    {"Guard", "Garde", "Wache", "Guardia", "Охрана"},
    {"Retreat", "Retraite", "Rückzug", "Retirada", "Отступление"},
    {"Upgrades", "Améliorations", "Aufrüstung", "Mejoras", "Улучшения"},
    {"Starport", "Astroport", "Raumhafen", "Astropuerto", "Космопорт"},
    {"Options", "Options", "Optionen", "Opciones", "Настройки"},
    {"Credits", "Crédits", "Credits", "Créditos", "Кредиты"},
    {"Power", "Énergie", "Energie", "Energía", "Энергия"},
    {"Mission timer", "Minuteur", "Missionszeit", "Tiempo de misión", "Таймер миссии"},
    {"Output", "Production", "Leistung", "Producción", "Выработка"},
    {"Demand", "Demande", "Bedarf", "Demanda", "Расход"},
    {"Cash", "Argent", "Bargeld", "Dinero", "Деньги"},
    {"Spice", "Épice", "Spice", "Especia", "Спайс"},
    {"Then click a building (or a damaged vehicle)", "Puis cliquez un bâtiment (ou un véhicule)",
     "Dann ein Gebäude (oder Fahrzeug) klicken", "Luego clic en un edificio (o vehículo)", "Затем щёлкните здание (или технику)"},
    {"Then click one of your buildings", "Puis cliquez un de vos bâtiments", "Dann eines Ihrer Gebäude klicken",
     "Luego clic en un edificio tuyo", "Затем щёлкните своё здание"},
    {"Selected units hold and guard their area", "Les unités gardent leur zone", "Einheiten bewachen ihr Gebiet",
     "Las unidades guardan su zona", "Отряд охраняет свой район"},
    {"Selected units fall back to the base", "Les unités rentrent à la base", "Einheiten ziehen sich zur Basis zurück",
     "Las unidades vuelven a la base", "Отряд отходит на базу"},
    {"Shows the building upgrades", "Affiche les améliorations", "Zeigt die Aufrüstungen", "Muestra las mejoras",
     "Показать улучшения"},
    {"Nothing can be upgraded now", "Rien à améliorer", "Nichts aufzurüsten", "Nada que mejorar", "Нечего улучшать"},
    {"Order units at today's prices (CHOAM)", "Commander au prix du jour (CHOAM)", "Einheiten zum Tagespreis (CHOAM)",
     "Unidades al precio de hoy (CHOAM)", "Заказ по текущим ценам (CHOAM)"},
    {"Needs a Starport", "Nécessite un astroport", "Benötigt einen Raumhafen", "Requiere un astropuerto", "Нужен космопорт"},
    {"Pause menu (Esc)", "Menu pause (Échap)", "Pausenmenü (Esc)", "Menú de pausa (Esc)", "Меню паузы (Esc)"},
    {"Below 100%: slower building, no radar", "Sous 100 % : construction lente, pas de radar", "Unter 100 %: langsamer Bau, kein Radar",
     "Bajo 100 %: obras lentas, sin radar", "Ниже 100%: медленная стройка, нет радара"},
    {"Refineries and silos store the spice", "Raffineries et silos stockent l'épice", "Raffinerien und Silos lagern Spice",
     "Refinerías y silos guardan la especia", "Спайс хранят заводы и хранилища"},
    {"Click to show this tab", "Cliquez pour afficher", "Klicken zum Anzeigen", "Clic para mostrar", "Щёлкните, чтобы открыть"},
    {"Nothing to build here yet", "Rien à construire ici", "Hier noch nichts zu bauen", "Aún nada que construir", "Пока нечего строить"},
    {"Scroll", "Défiler", "Blättern", "Desplazar", "Прокрутка"},
    {"Right click: a page", "Clic droit : une page", "Rechtsklick: eine Seite", "Clic derecho: una página", "ПКМ: страница"},
    {"Left: build", "Gauche : construire", "Links: bauen", "Izq.: construir", "ЛКМ: строить"},
    {"Left: build   Right: hold / cancel", "Gauche : construire   Droit : pause / annuler", "Links: bauen   Rechts: Halt / Abbruch",
     "Izq.: construir   Der.: pausa / cancelar", "ЛКМ: строить   ПКМ: пауза / отмена"},
    {"Ready - left click, then place it", "Prêt - cliquez puis placez-le", "Bereit - klicken, dann platzieren",
     "Listo - clic y colócalo", "Готово - щёлкните и разместите"},
    {"Left: pick   Right: unpick", "Gauche : choisir   Droit : retirer", "Links: wählen   Rechts: abwählen",
     "Izq.: elegir   Der.: quitar", "ЛКМ: выбрать   ПКМ: убрать"},
    {"Pay and send the frigate with the picks", "Payer et envoyer la frégate", "Bezahlen und Fregatte senden",
     "Pagar y enviar la fragata", "Оплатить и вызвать фрегат"},
    {"Drop all picks", "Tout retirer", "Alle abwählen", "Quitar todo", "Сбросить выбор"},
    {"Building upgrades", "Améliorations", "Gebäude-Aufrüstung", "Mejoras de edificios", "Улучшения зданий"},
    {"Upgrades unlock new units and structures", "Débloquent unités et bâtiments", "Schalten Einheiten und Gebäude frei",
     "Desbloquean unidades y edificios", "Открывают новые войска и здания"},
    {"Special weapons, launched when charged", "Armes spéciales, tirées une fois prêtes", "Spezialwaffen, bereit nach Aufladung",
     "Armas especiales, listas al cargar", "Спецоружие, пуск после зарядки"},
    {"Purchase", "Acheter", "Kaufen", "Comprar", "Купить"},
    {"Cancel", "Annuler", "Abbruch", "Cancelar", "Отмена"},
    {"Left: upgrade", "Gauche : améliorer", "Links: aufrüsten", "Izq.: mejorar", "ЛКМ: улучшить"},
    {"Another upgrade is running", "Une autre amélioration en cours", "Andere Aufrüstung läuft", "Otra mejora en curso", "Идёт другое улучшение"},
    {"On hold", "En pause", "Angehalten", "En pausa", "На паузе"},
    {"Left: resume   Right: cancel", "Gauche : reprendre   Droit : annuler", "Links: weiter   Rechts: Abbruch",
     "Izq.: seguir   Der.: cancelar", "ЛКМ: продолжить   ПКМ: отмена"},
    {"Right: hold", "Droit : pause", "Rechts: Halt", "Der.: pausa", "ПКМ: пауза"},
    {"On hold - left: resume, right: cancel", "En pause - gauche : reprendre, droit : annuler", "Halt - links: weiter, rechts: Abbruch",
     "En pausa - izq.: seguir, der.: cancelar", "Пауза - ЛКМ: продолжить, ПКМ: отмена"},
    {"Ready - left click to launch", "Prêt - cliquez pour lancer", "Bereit - klicken zum Start", "Listo - clic para lanzar", "Готово - щёлкните для пуска"},
    {"Charging (palace / airstrike)", "En charge (palais / frappe)", "Lädt (Palast / Luftschlag)", "Cargando (palacio / ataque)",
     "Зарядка (дворец / авиаудар)"},
    {"Left: +1  Shift: +10  Right: repeat", "Gauche : +1  Maj : +10  Droit : répéter", "Links: +1  Umschalt: +10  Rechts: wiederholen",
     "Izq.: +1  Mayús: +10  Der.: repetir", "ЛКМ: +1  Shift: +10  ПКМ: повтор"},
// Per-area tables (one file each, so they can be edited independently).
#include "render/ui_text_common.inc"
#include "render/ui_text_menu.inc"
#include "render/ui_text_options.inc"
#include "render/ui_text_game.inc"
#include "render/ui_text_cheats.inc"
#include "render/ui_text_artpack.inc"
#include "render/ui_text_welcome.inc"
};

int lang = 0;  // 0 en, 1 fr, 2 de, 3 es, 4 ru

const std::unordered_map<std::string_view, const Row*>& table() {
    static const std::unordered_map<std::string_view, const Row*> t = [] {
        std::unordered_map<std::string_view, const Row*> m;
        for (const Row& r : kRows) m.emplace(r.en, &r);
        return m;
    }();
    return t;
}

}  // namespace

void setUiLanguage(const std::string& code) {
    lang = code == "fr" ? 1 : code == "de" ? 2 : code == "es" ? 3 : code.rfind("ru", 0) == 0 ? 4 : 0;
}

int uiLanguage() { return lang; }

std::string trf(const char* englishFormat, ...) {
    const char* f = tr(englishFormat);
    char buf[1024];
    va_list ap;
    va_start(ap, englishFormat);
    std::vsnprintf(buf, sizeof buf, f, ap);
    va_end(ap);
    return buf;
}

const char* tr(const char* english) {
    if (lang == 0 || !english) return english;
    const auto& t = table();
    const auto it = t.find(english);
    if (it == t.end()) return english;
    const Row& r = *it->second;
    const char* s = lang == 1 ? r.fr : lang == 2 ? r.de : lang == 3 ? r.es : r.ru;
    return s && *s ? s : english;
}

}  // namespace d2k::render
