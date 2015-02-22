/**
 * All includes copied from autolabor for now
 */
#include "Core.h"
#include <Console.h>
#include <Export.h>
#include <PluginManager.h>

#include <vector>
#include <algorithm>

#include "modules/Units.h"
#include "modules/World.h"

// DF data structure definition headers
#include "DataDefs.h"
#include <df/ui.h>
#include <df/world.h>
#include <df/unit.h>
#include <df/unit_soul.h>
#include <df/unit_labor.h>
#include <df/unit_skill.h>
#include <df/job.h>
#include <df/building.h>
#include <df/workshop_type.h>
#include <df/unit_misc_trait.h>
#include <df/entity_position_responsibility.h>
#include <df/historical_figure.h>
#include <df/historical_entity.h>
#include <df/histfig_entity_link.h>
#include <df/histfig_entity_link_positionst.h>
#include <df/entity_position_assignment.h>
#include <df/entity_position.h>
#include <df/building_tradedepotst.h>
#include <df/building_stockpilest.h>
#include <df/items_other_id.h>
#include <df/ui.h>
#include <df/activity_info.h>

#include <MiscUtils.h>

#include "modules/MapCache.h"
#include "modules/Items.h"
#include "modules/Units.h"

// Not sure what this does, but may have to figure it out later
#define ARRAY_COUNT(array) (sizeof(array)/sizeof((array)[0]))

// I can see a reason for having all of these
using std::string;
using std::endl;
using std::vector;
using namespace DFHack;
using namespace df::enums;

// idk what this does
DFHACK_PLUGIN("autohauler");
REQUIRE_GLOBAL(ui);
REQUIRE_GLOBAL(world);

/*
 * Autohauler module for dfhack
 * Fork of autolabor
 *
 * Rather than the all-of-the-above means of autolabor, autohauler will instead
 * only manage hauling labors and leave skilled labors entirely to the user, who
 * will probably use Dwarf Therapist to do so. 
 * Idle dwarves will be assigned the hauling labors; everyone else (including
 * those currently hauling) will have the hauling labors removed. This is to
 * encourage every dwarf to do their assigned skilled labors whenever possible,
 * but resort to hauling when those jobs are not available. This also implies
 * that the user will have a very tight skill assignment, with most skilled
 * labors only being assigned to just one dwarf, no dwarf having more than two
 * active skilled labors, and almost every non-military dwarf having at least
 * one skilled labor assigned.
 * It is noteworthy that, as stated in autolabor.cpp, "for almost all labors, 
 * once a dwarf begins a job it will finish that job even if the associated
 * labor is removed." This is why we can remove hauling labors by default to try
 * to force dwarves to do "real" jobs whenever they can.
 * This is a standalone plugin. However, it would be wise to delete
 * autolabor.plug.dll as this plugin is mutually exclusive with it.
 */

// Yep...don't know what it does
DFHACK_PLUGIN_IS_ENABLED(enable_autohauler);

// This is the configuration saved into the world save file
static PersistentDataItem config;
 
// Don't know what this does
command_result autohauler (color_ostream &out, std::vector <std::string> & parameters);

// Don't know what this does either
static bool isOptionEnabled(unsigned flag)
{
    return config.isValid() && (config.ival(0) & flag) != 0;
}

// Not sure about the purpose of this
enum ConfigFlags {
    CF_ENABLED = 1,
};

// Don't know what this does
static void setOptionEnabled(ConfigFlags flag, bool on)
{
    if (!config.isValid())
        return;

    if (on)
        config.ival(0) |= flag;
    else
        config.ival(0) &= ~flag;
}

// There is a possibility I will add extensive, line-by-line debug capability
// later
static bool print_debug = false;

// Not sure what it does but it's probably related to the following enumeration
static std::vector<int> state_count(5);

// Employment status of dwarves
// xxx Shouldn't this be static?
enum dwarf_state {
    // Ready for a new task
    IDLE,

    // Busy with a useful task
    BUSY,

    // In the military, can't work
    MILITARY,

    // Baby or Child, can't work
    CHILD,

    // Doing something that precludes working, may be busy for a while
    OTHER
};

// I presume this is the number of states in the following enumeration.
// xxx Shouldn't this be static?
const int NUM_STATE = 5;

// This is a list of strings to be associated with aforementioned dwarf_state
// struct
static const char *state_names[] = {
    "IDLE",
    "BUSY",
    "MILITARY",
    "CHILD",
    "OTHER",
};

// List of possible activites of a dwarf that will be further narrowed to states
static const dwarf_state dwarf_states[] = {
    BUSY /* CarveFortification */,
    BUSY /* DetailWall */,
    BUSY /* DetailFloor */,
    BUSY /* Dig */,
    BUSY /* CarveUpwardStaircase */,
    BUSY /* CarveDownwardStaircase */,
    BUSY /* CarveUpDownStaircase */,
    BUSY /* CarveRamp */,
    BUSY /* DigChannel */,
    BUSY /* FellTree */,
    BUSY /* GatherPlants */,
    BUSY /* RemoveConstruction */,
    BUSY /* CollectWebs */,
    BUSY /* BringItemToDepot */,
    BUSY /* BringItemToShop */,
    OTHER /* Eat */,
    OTHER /* GetProvisions */,
    OTHER /* Drink */,
    OTHER /* Drink2 */,
    OTHER /* FillWaterskin */,
    OTHER /* FillWaterskin2 */,
    OTHER /* Sleep */,
    BUSY /* CollectSand */,
    BUSY /* Fish */,
    BUSY /* Hunt */,
    OTHER /* HuntVermin */,
    BUSY /* Kidnap */,
    BUSY /* BeatCriminal */,
    BUSY /* StartingFistFight */,
    BUSY /* CollectTaxes */,
    BUSY /* GuardTaxCollector */,
    BUSY /* CatchLiveLandAnimal */,
    BUSY /* CatchLiveFish */,
    BUSY /* ReturnKill */,
    BUSY /* CheckChest */,
    BUSY /* StoreOwnedItem */,
    BUSY /* PlaceItemInTomb */,
    BUSY /* StoreItemInStockpile */,
    BUSY /* StoreItemInBag */,
    BUSY /* StoreItemInHospital */,
    BUSY /* StoreItemInChest */,
    BUSY /* StoreItemInCabinet */,
    BUSY /* StoreWeapon */,
    BUSY /* StoreArmor */,
    BUSY /* StoreItemInBarrel */,
    BUSY /* StoreItemInBin */,
    BUSY /* SeekArtifact */,
    BUSY /* SeekInfant */,
    OTHER /* AttendParty */,
    OTHER /* GoShopping */,
    OTHER /* GoShopping2 */,
    BUSY /* Clean */,
    OTHER /* Rest */,
    BUSY /* PickupEquipment */,
    BUSY /* DumpItem */,
    OTHER /* StrangeMoodCrafter */,
    OTHER /* StrangeMoodJeweller */,
    OTHER /* StrangeMoodForge */,
    OTHER /* StrangeMoodMagmaForge */,
    OTHER /* StrangeMoodBrooding */,
    OTHER /* StrangeMoodFell */,
    OTHER /* StrangeMoodCarpenter */,
    OTHER /* StrangeMoodMason */,
    OTHER /* StrangeMoodBowyer */,
    OTHER /* StrangeMoodTanner */,
    OTHER /* StrangeMoodWeaver */,
    OTHER /* StrangeMoodGlassmaker */,
    OTHER /* StrangeMoodMechanics */,
    BUSY /* ConstructBuilding */,
    BUSY /* ConstructDoor */,
    BUSY /* ConstructFloodgate */,
    BUSY /* ConstructBed */,
    BUSY /* ConstructThrone */,
    BUSY /* ConstructCoffin */,
    BUSY /* ConstructTable */,
    BUSY /* ConstructChest */,
    BUSY /* ConstructBin */,
    BUSY /* ConstructArmorStand */,
    BUSY /* ConstructWeaponRack */,
    BUSY /* ConstructCabinet */,
    BUSY /* ConstructStatue */,
    BUSY /* ConstructBlocks */,
    BUSY /* MakeRawGlass */,
    BUSY /* MakeCrafts */,
    BUSY /* MintCoins */,
    BUSY /* CutGems */,
    BUSY /* CutGlass */,
    BUSY /* EncrustWithGems */,
    BUSY /* EncrustWithGlass */,
    BUSY /* DestroyBuilding */,
    BUSY /* SmeltOre */,
    BUSY /* MeltMetalObject */,
    BUSY /* ExtractMetalStrands */,
    BUSY /* PlantSeeds */,
    BUSY /* HarvestPlants */,
    BUSY /* TrainHuntingAnimal */,
    BUSY /* TrainWarAnimal */,
    BUSY /* MakeWeapon */,
    BUSY /* ForgeAnvil */,
    BUSY /* ConstructCatapultParts */,
    BUSY /* ConstructBallistaParts */,
    BUSY /* MakeArmor */,
    BUSY /* MakeHelm */,
    BUSY /* MakePants */,
    BUSY /* StudWith */,
    BUSY /* ButcherAnimal */,
    BUSY /* PrepareRawFish */,
    BUSY /* MillPlants */,
    BUSY /* BaitTrap */,
    BUSY /* MilkCreature */,
    BUSY /* MakeCheese */,
    BUSY /* ProcessPlants */,
    BUSY /* ProcessPlantsBag */,
    BUSY /* ProcessPlantsVial */,
    BUSY /* ProcessPlantsBarrel */,
    BUSY /* PrepareMeal */,
    BUSY /* WeaveCloth */,
    BUSY /* MakeGloves */,
    BUSY /* MakeShoes */,
    BUSY /* MakeShield */,
    BUSY /* MakeCage */,
    BUSY /* MakeChain */,
    BUSY /* MakeFlask */,
    BUSY /* MakeGoblet */,
    BUSY /* MakeInstrument */,
    BUSY /* MakeToy */,
    BUSY /* MakeAnimalTrap */,
    BUSY /* MakeBarrel */,
    BUSY /* MakeBucket */,
    BUSY /* MakeWindow */,
    BUSY /* MakeTotem */,
    BUSY /* MakeAmmo */,
    BUSY /* DecorateWith */,
    BUSY /* MakeBackpack */,
    BUSY /* MakeQuiver */,
    BUSY /* MakeBallistaArrowHead */,
    BUSY /* AssembleSiegeAmmo */,
    BUSY /* LoadCatapult */,
    BUSY /* LoadBallista */,
    BUSY /* FireCatapult */,
    BUSY /* FireBallista */,
    BUSY /* ConstructMechanisms */,
    BUSY /* MakeTrapComponent */,
    BUSY /* LoadCageTrap */,
    BUSY /* LoadStoneTrap */,
    BUSY /* LoadWeaponTrap */,
    BUSY /* CleanTrap */,
    BUSY /* CastSpell */,
    BUSY /* LinkBuildingToTrigger */,
    BUSY /* PullLever */,
    BUSY /* BrewDrink */,
    BUSY /* ExtractFromPlants */,
    BUSY /* ExtractFromRawFish */,
    BUSY /* ExtractFromLandAnimal */,
    BUSY /* TameVermin */,
    BUSY /* TameAnimal */,
    BUSY /* ChainAnimal */,
    BUSY /* UnchainAnimal */,
    BUSY /* UnchainPet */,
    BUSY /* ReleaseLargeCreature */,
    BUSY /* ReleasePet */,
    BUSY /* ReleaseSmallCreature */,
    BUSY /* HandleSmallCreature */,
    BUSY /* HandleLargeCreature */,
    BUSY /* CageLargeCreature */,
    BUSY /* CageSmallCreature */,
    BUSY /* RecoverWounded */,
    BUSY /* DiagnosePatient */,
    BUSY /* ImmobilizeBreak */,
    BUSY /* DressWound */,
    BUSY /* CleanPatient */,
    BUSY /* Surgery */,
    BUSY /* Suture */,
    BUSY /* SetBone */,
    BUSY /* PlaceInTraction */,
    BUSY /* DrainAquarium */,
    BUSY /* FillAquarium */,
    BUSY /* FillPond */,
    BUSY /* GiveWater */,
    BUSY /* GiveFood */,
    BUSY /* GiveWater2 */,
    BUSY /* GiveFood2 */,
    BUSY /* RecoverPet */,
    BUSY /* PitLargeAnimal */,
    BUSY /* PitSmallAnimal */,
    BUSY /* SlaughterAnimal */,
    BUSY /* MakeCharcoal */,
    BUSY /* MakeAsh */,
    BUSY /* MakeLye */,
    BUSY /* MakePotashFromLye */,
    BUSY /* FertilizeField */,
    BUSY /* MakePotashFromAsh */,
    BUSY /* DyeThread */,
    BUSY /* DyeCloth */,
    BUSY /* SewImage */,
    BUSY /* MakePipeSection */,
    BUSY /* OperatePump */,
    OTHER /* ManageWorkOrders */,
    OTHER /* UpdateStockpileRecords */,
    OTHER /* TradeAtDepot */,
    BUSY /* ConstructHatchCover */,
    BUSY /* ConstructGrate */,
    BUSY /* RemoveStairs */,
    BUSY /* ConstructQuern */,
    BUSY /* ConstructMillstone */,
    BUSY /* ConstructSplint */,
    BUSY /* ConstructCrutch */,
    BUSY /* ConstructTractionBench */,
    BUSY /* CleanSelf */,
    BUSY /* BringCrutch */,
    BUSY /* ApplyCast */,
    BUSY /* CustomReaction */,
    BUSY /* ConstructSlab */,
    BUSY /* EngraveSlab */,
    BUSY /* ShearCreature */,
    BUSY /* SpinThread */,
    BUSY /* PenLargeAnimal */,
    BUSY /* PenSmallAnimal */,
    BUSY /* MakeTool */,
    BUSY /* CollectClay */,
    BUSY /* InstallColonyInHive */,
    BUSY /* CollectHiveProducts */,
    OTHER /* CauseTrouble */,
    OTHER /* DrinkBlood */,
    OTHER /* ReportCrime */,
    OTHER /* ExecuteCriminal */,
    BUSY /* TrainAnimal */,
    BUSY /* CarveTrack */,
    BUSY /* PushTrackVehicle */,
    BUSY /* PlaceTrackVehicle */,
    BUSY /* StoreItemInVehicle */,
    BUSY /* GeldAnimal */
};

// Mode assigned to labors. Either it's a hauling job, or it's not.
// xxx Shouldn't this be static?
enum labor_mode {
    DISABLE,
    HAULERS,
};

// This is the default treatment of a particular labor.
struct labor_default
{
    labor_mode mode;
    int active_dwarfs;
};

// This is the current treatment of a particular labor.
// This would have been more cleanly presented as a class
struct labor_info
{
    // It seems as if this is the means of accessing the world data
    PersistentDataItem config;

    // Number of dwarves assigned this labor
    int active_dwarfs;

    // Set the persistent data item associated with this labor treatment
    // We Java folk hate pointers, but that's what the parameter will be
    void set_config(PersistentDataItem a) { config = a; }
    
    // Return the labor_mode associated with this labor
    labor_mode mode() { return (labor_mode) config.ival(0); }
    
    // Set the labor_mode associated with this labor
    void set_mode(labor_mode mode) { config.ival(0) = mode; }
};

// This is a vector of all the current labor treatments
static std::vector<struct labor_info> labor_infos;

// This is just an array of all the labors, whether it should be untouched
// (DISABLE) or treated as a last-resort job (HAULERS).
static const struct labor_default default_labor_infos[] = {
    /* MINE */                  {DISABLE, 0},
    /* HAUL_STONE */            {HAULERS, 0},
    /* HAUL_WOOD */             {HAULERS, 0},
    /* HAUL_BODY */             {HAULERS, 0},
    /* HAUL_FOOD */             {HAULERS, 0},
    /* HAUL_REFUSE */           {HAULERS, 0},
    /* HAUL_ITEM */             {HAULERS, 0},
    /* HAUL_FURNITURE */        {HAULERS, 0},
    /* HAUL_ANIMAL */           {HAULERS, 0},
    /* CLEAN */                 {HAULERS, 0},
    /* CUTWOOD */               {DISABLE, 0},
    /* CARPENTER */             {DISABLE, 0},
    /* DETAIL */                {DISABLE, 0},
    /* MASON */                 {DISABLE, 0},
    /* ARCHITECT */             {DISABLE, 0},
    /* ANIMALTRAIN */           {DISABLE, 0},
    /* ANIMALCARE */            {DISABLE, 0},
    /* DIAGNOSE */              {DISABLE, 0},
    /* SURGERY */               {DISABLE, 0},
    /* BONE_SETTING */          {DISABLE, 0},
    /* SUTURING */              {DISABLE, 0},
    /* DRESSING_WOUNDS */       {DISABLE, 0},
    /* FEED_WATER_CIVILIANS */  {DISABLE, 0},
    /* RECOVER_WOUNDED */       {HAULERS, 0},
    /* BUTCHER */               {DISABLE, 0},
    /* TRAPPER */               {DISABLE, 0},
    /* DISSECT_VERMIN */        {DISABLE, 0},
    /* LEATHER */               {DISABLE, 0},
    /* TANNER */                {DISABLE, 0},
    /* BREWER */                {DISABLE, 0},
    /* ALCHEMIST */             {DISABLE, 0},
    /* SOAP_MAKER */            {DISABLE, 0},
    /* WEAVER */                {DISABLE, 0},
    /* CLOTHESMAKER */          {DISABLE, 0},
    /* MILLER */                {DISABLE, 0},
    /* PROCESS_PLANT */         {DISABLE, 0},
    /* MAKE_CHEESE */           {DISABLE, 0},
    /* MILK */                  {DISABLE, 0},
    /* COOK */                  {DISABLE, 0},
    /* PLANT */                 {DISABLE, 0},
    /* HERBALIST */             {DISABLE, 0},
    /* FISH */                  {DISABLE, 0},
    /* CLEAN_FISH */            {DISABLE, 0},
    /* DISSECT_FISH */          {DISABLE, 0},
    /* HUNT */                  {DISABLE, 0},
    /* SMELT */                 {DISABLE, 0},
    /* FORGE_WEAPON */          {DISABLE, 0},
    /* FORGE_ARMOR */           {DISABLE, 0},
    /* FORGE_FURNITURE */       {DISABLE, 0},
    /* METAL_CRAFT */           {DISABLE, 0},
    /* CUT_GEM */               {DISABLE, 0},
    /* ENCRUST_GEM */           {DISABLE, 0},
    /* WOOD_CRAFT */            {DISABLE, 0},
    /* STONE_CRAFT */           {DISABLE, 0},
    /* BONE_CARVE */            {DISABLE, 0},
    /* GLASSMAKER */            {DISABLE, 0},
    /* EXTRACT_STRAND */        {DISABLE, 0},
    /* SIEGECRAFT */            {DISABLE, 0},
    /* SIEGEOPERATE */          {DISABLE, 0},
    /* BOWYER */                {DISABLE, 0},
    /* MECHANIC */              {DISABLE, 0},
    /* POTASH_MAKING */         {DISABLE, 0},
    /* LYE_MAKING */            {DISABLE, 0},
    /* DYER */                  {DISABLE, 0},
    /* BURN_WOOD */             {DISABLE, 0},
    /* OPERATE_PUMP */          {DISABLE, 0},
    /* SHEARER */               {DISABLE, 0},
    /* SPINNER */               {DISABLE, 0},
    /* POTTERY */               {DISABLE, 0},
    /* GLAZING */               {DISABLE, 0},
    /* PRESSING */              {DISABLE, 0},
    /* BEEKEEPING */            {DISABLE, 0},
    /* WAX_WORKING */           {DISABLE, 0},
    /* HANDLE_VEHICLES */       {HAULERS, 0},
    /* HAUL_TRADE */            {HAULERS, 0},
    /* PULL_LEVER */            {HAULERS, 0},
    /* REMOVE_CONSTRUCTION */   {HAULERS, 0},
    /* HAUL_WATER */            {HAULERS, 0},
    /* GELD */                  {DISABLE, 0},
    /* BUILD_ROAD */            {HAULERS, 0},
    /* BUILD_CONSTRUCTION */    {HAULERS, 0}
};

/**
 * Reset labor to default treatment
 */
static void reset_labor(df::unit_labor labor)
{
    labor_infos[labor].set_mode(default_labor_infos[labor].mode);
}

/**
 * A 
 */
struct dwarf_info_t
{
    int assigned_jobs;
    dwarf_state state;
};

/**
 * Disable autohauler labor lists
 */
static void cleanup_state()
{
    enable_autohauler = false;
    labor_infos.clear();
}

/**
 * Initialize the plugin labor lists
 */
static void init_state()
{
    // This obtains the persistent data from the world save file
    config = World::GetPersistentData("autohauler/config");
    
    // xxx I don't know what this does
    if (config.isValid() && config.ival(0) == -1)
        config.ival(0) = 0;

    // Check to see if the plugin is enabled in the persistent data, if so then
    // enable the local flag for autohauler being enabled
    enable_autohauler = isOptionEnabled(CF_ENABLED);

    // If autohauler is not enabled then it's pretty pointless to do the rest
    if (!enable_autohauler)
        return;

    /* Here we are going to populate the labor list by loading persistent data
     * from the world save */
    
    // This is a vector of all the persistent data items from config
    std::vector<PersistentDataItem> items;
    
    // This populates the aforementioned vector
    World::GetPersistentData(&items, "autohauler/labors/", true);
    
    // Resize the list of current labor treatments to size of list of default
    // labor treatments
    labor_infos.resize(ARRAY_COUNT(default_labor_infos));

    // For every persistent data item...
    for (auto p = items.begin(); p != items.end(); p++)
    {
        // Load as a string the key associated with the persistent data item
        string key = p->key();
        
        // Translate the string into a labor defined by global dfhack constants
        df::unit_labor labor = (df::unit_labor) atoi(key.substr(strlen("autohauler/labors/")).c_str());
        
        // This is a vector of all the current labor treatments
        
        // Ensure that the labor is defined in the existing list
        if (labor >= 0 && labor <= labor_infos.size())
        {
            // Link the labor treatment with the associated persistent data item
            labor_infos[labor].set_config(*p);
            
            
            labor_infos[labor].active_dwarfs = 0;
        }
    }

    // Add default labors for those not in save
    for (int i = 0; i < ARRAY_COUNT(default_labor_infos); i++) {
        
        // Determine if the labor is already present. If so, exit the for loop
        if (labor_infos[i].config.isValid())
            continue;

        // Not sure of the mechanics, but it seems to give an output stream
        // giving a string for the new persistent data item
        std::stringstream name;
        name << "autohauler/labors/" << i;

        // Add a new persistent data item as it is not currently in the save
        labor_infos[i].set_config(World::AddPersistentData(name.str()));

        // Set the active counter to zero
        labor_infos[i].active_dwarfs = 0;
        
        // Reset labor to default treatment
        reset_labor((df::unit_labor) i);
    }
    
    // xxx I don't see the use of the following line
    // generate_labor_to_skill_map();

}

/**
 * Call this method to enable the plugin.
 */
static void enable_plugin(color_ostream &out)
{
    // If there is no config file, make one
    if (!config.isValid())
    {
        config = World::AddPersistentData("autohauler/config");
        config.ival(0) = 0;
    }

    // xxx I think this is already done in init_state(), but it can't hurt
    // xxx Also, aren't these two redundant?
    setOptionEnabled(CF_ENABLED, true);
    enable_autohauler = true;
    
    // Output to console that the plugin is enabled
    out << "Enabling the plugin." << endl;

    // Disable autohauler and clear the labor list
    cleanup_state();
    
    // Initialize the plugin
    init_state();
}

/**
 * Initialize the plugin
 */
DFhackCExport command_result plugin_init ( color_ostream &out, std::vector <PluginCommand> &commands)
{
    // This seems to verify that the default labor list and the current labor
    // list are the same size
    if(ARRAY_COUNT(default_labor_infos) != ENUM_LAST_ITEM(unit_labor) + 1)
    {
        out.printerr("autohauler: labor size mismatch\n");
        return CR_FAILURE;
    }

    // Essentially an introduction dumped to the console
    commands.push_back(PluginCommand(
        "autohauler", "Automatically manage hauling labors.",
        autohauler, false, /* true means that the command can't be used from non-interactive user interface */
        // Extended help string. Used by CR_WRONG_USAGE and the help command:
        "  autohauler enable\n"
        "  autohauler disable\n"
        "    Enables or disables the plugin.\n"
        "  autohauler <labor> haulers\n"
        "    Set a labor to be handled by hauler dwarves.\n"
        "  autohauler <labor> disable\n"
        "    Turn off autohauler for a specific labor.\n"
        "  autohauler <labor> reset\n"
        "    Return a labor to the default handling.\n"
        "  autohauler reset-all\n"
        "    Return all labors to the default handling.\n"
        "  autohauler list\n"
        "    List current status of all labors.\n"
        "  autohauler status\n"
        "    Show basic status information.\n"
        "Function:\n"
        "  When enabled, autohauler periodically checks your dwarves and assigns\n"
        "  hauling jobs to idle dwarves while removing them from busy dwarves.\n"
        "  This plugin, in contrast to autolabor, is explicitly designed to be\n"
        "  used alongside Dwarf Therapist.\n"
        "  Warning: autohauler will override any manual changes you make to\n"
        "  hauling labors while it is enabled...but why would you make them?\n"
        "Examples:\n"
        "  autohauler HAUL_STONE haulers\n"
        "    Set stone hauling as a hauling labor.\n"
        "  autohauler FEED_WATER_CIVILIANS disable\n"
        "    Disable plugin handling of feeding patients and prisoners.\n"
    ));

    // Initialize plugin labor lists
    init_state();

    return CR_OK;
}

/**
 * Initialize the plugin
 */
DFhackCExport command_result plugin_shutdown ( color_ostream &out )
{
    cleanup_state();

    return CR_OK;
}

/**
 * This method responds to the map being loaded, or unloaded.
 */
DFhackCExport command_result plugin_onstatechange(color_ostream &out, state_change_event event)
{
    switch (event) {
    case SC_MAP_LOADED:
        cleanup_state();
        init_state();
        break;
    case SC_MAP_UNLOADED:
        cleanup_state();
        break;
    default:
        break;
    }

    return CR_OK;
}

/**
 * This method is called every frame in Dwarf Fortress from my understanding.
 */
DFhackCExport command_result plugin_onupdate ( color_ostream &out )
{
    // This makes it so that the plugin is only run every 60 steps, in order to
    // save FPS. Since it is static, this is declared before this method is called
    static int step_count = 0;
    
    // Cancel run if the world doesn't exist or plugin isn't enabled
    if(!world || !world->map.block_index || !enable_autohauler) { return CR_OK; }

    // Increment step count
    step_count++;
    
    // Run aforementioned step count and return unless threshold is reached.
    // xxx We may want this to be a constant
    if (step_count < 60) return CR_OK;
    
    // Reset step count since at this point it has reached 60
    step_count = 0;

    // xxx I don't know what this does
    uint32_t race = ui->race_id;
    uint32_t civ = ui->civ_id;

    // Create a vector of units. This will be populated in the following for loop.
    std::vector<df::unit *> dwarfs;

    // Scan the world and look for any citizens in the player's civilization.
    // Add these to the list of dwarves.
    // xxx Does it need to be ++i?
    for (int i = 0; i < world->units.active.size(); ++i)
    {
        df::unit* cre = world->units.active[i];
        if (Units::isCitizen(cre))
        {
            dwarfs.push_back(cre);
        }
    }

    // This just keeps track of the number of civilians from the previous for loop.
    int n_dwarfs = dwarfs.size();

    // This will return if there are no civilians. Otherwise could call 
    // nonexistent elements of array.
    if (n_dwarfs == 0)
        return CR_OK;

    // This is a matching of assigned jobs with a dwarf's state
    // xxx but wouldn't it be better if this and "dwarfs" were in the same object?
    std::vector<dwarf_info_t> dwarf_info(n_dwarfs);

    // Reset the counter for number of dwarves in states to zero
    state_count.clear();
    state_count.resize(NUM_STATE);

    // Find the activity state for each dwarf
    for (int dwarf = 0; dwarf < n_dwarfs; dwarf++)
    {

        // Default deny condition of on break for later else-if series
		bool is_on_break = false;

		// Scan a dwarf's miscellaneous traits for on break or migrant status.
		// If either of these are present, disable hauling because we want them
		// to try to find real jobs first
		for (auto p = dwarfs[dwarf]->status.misc_traits.begin(); p < dwarfs[dwarf]->status.misc_traits.end(); p++)
		{
			if ((*p)->id == misc_trait_type::Migrant || (*p)->id == misc_trait_type::OnBreak)
				is_on_break = true;
		}
		
		// I don't think you can set the labors for babies and children, but let's
        // ignore them anyway
        if (Units::isBaby(dwarfs[dwarf]) || Units::isChild(dwarfs[dwarf]))
        {
            dwarf_info[dwarf].state = CHILD;
        }
		// Don't give hauling jobs to dwarves on break or migrants
		else if (is_on_break)
		{
			dwarf_info[dwarf].state = OTHER;
		}
        // Don't give hauling jobs to the military either
        else if (ENUM_ATTR(profession, military, dwarfs[dwarf]->profession))
            dwarf_info[dwarf].state = MILITARY;
		// Dwarf is unemployed with null job
        else if (dwarfs[dwarf]->job.current_job == NULL)
        {
            // xxx Figure out what specific_refs is
            //if (dwarfs[dwarf]->specific_refs.size() > 0)
            //    dwarf_info[dwarf].state = OTHER;
            // If no job is found then they are officially idle and open for hauling
            //else
                dwarf_info[dwarf].state = IDLE;
        }
        // If it gets to this point the dwarf is employed
        else
        {
            int job = dwarfs[dwarf]->job.current_job->job_type;
            if (job >= 0 && job < ARRAY_COUNT(dwarf_states))
                dwarf_info[dwarf].state = dwarf_states[job];
            else
            {
                // Warn the console that the dwarf has an unregistered labor, default to OTHER
                out.print("Dwarf %i \"%s\" has unknown job %i\n", dwarf, dwarfs[dwarf]->name.first_name.c_str(), job);
                dwarf_info[dwarf].state = OTHER;
            }
        }

        // Increment corresponding labor in default_labor_infos struct
        state_count[dwarf_info[dwarf].state]++;
        
    }

    // This is a vector of all the labors
    std::vector<df::unit_labor> labors;

    // For every labor...
    FOR_ENUM_ITEMS(unit_labor, labor)
    {
        // Ignore all nonexistent labors
        if (labor == unit_labor::NONE)
            continue;

        // Set number of active dwarves for this job to zero
        labor_infos[labor].active_dwarfs = 0;

        // And add the labor to the aforementioned vector of labors
        labors.push_back(labor);
    }

    // This is a different algorithm than Autolabor. Instead, the intent is to
    // have "real" jobs filled first, then if nothing is available the dwarf
    // instead resorts to hauling.
    
    // IDLE - Enable hauling
    // BUSY - Disable hauling
    // OTHER - Disable hauling

    // This is a vector of potential hauler IDs
    std::vector<int> hauler_ids;
    
    // Pretty much we are only considering non-military, non-child dwarves
    for (int dwarf = 0; dwarf < n_dwarfs; dwarf++)
    {
        if (dwarf_info[dwarf].state == IDLE || 
            dwarf_info[dwarf].state == BUSY ||
            dwarf_info[dwarf].state == OTHER) hauler_ids.push_back(dwarf);
    }

    // Equivalent of Java for(unit_labor : labor)
    // For every labor...
    FOR_ENUM_ITEMS(unit_labor, labor) 
    {
        // If this is a non-labor skip this for loop
        if (labor == unit_labor::NONE)
            continue;

        // If this is not a hauling labor then skip this for loop
        if (labor_infos[labor].mode() != HAULERS)
            continue;

        // For every dwarf...
        for(int dwarf = 0; dwarf < dwarfs.size(); dwarf++)
        {
            // xxx I don't think this was necessary?
			// int dwarf = dwarfs[i];
            
            // If the dwarf is idle, enable the hauling labor
            if(dwarf_info[dwarf].state == IDLE)
            {
                // Only increment assignment counter if job wasn't present before
                if(!dwarfs[dwarf]->status.labors[labor])
				{
					dwarf_info[dwarf].assigned_jobs++;
				}
                labor_infos[labor].active_dwarfs++;    
                dwarfs[dwarf]->status.labors[labor] = true;
            }
            // If the dwarf is busy, disable the hauling labor
            if(dwarf_info[dwarf].state == BUSY || dwarf_info[dwarf].state == OTHER)
            {               
                dwarfs[dwarf]->status.labors[labor] = false;
            }
            
        }

	// Let's play a game of "find the missing bracket!" I hope this is correct.
	}

    // This would be the last statement of the method
    return CR_OK;
}

/**
 * xxx Isn't this a repeat of enable_plugin? If it is separately called, then
 * passing a constructor should suffice.
 */
DFhackCExport command_result plugin_enable ( color_ostream &out, bool enable )
{
    if (!Core::getInstance().isWorldLoaded()) {
        out.printerr("World is not loaded: please load a game first.\n");
        return CR_FAILURE;
    }

    if (enable && !enable_autohauler)
    {
        enable_plugin(out);
    }
    else if(!enable && enable_autohauler)
    {
        enable_autohauler = false;
        setOptionEnabled(CF_ENABLED, false);

        out << "Autohauler is disabled." << endl;
    }

    return CR_OK;
}

/**
 * Print the aggregate labor status to the console.
 */
void print_labor (df::unit_labor labor, color_ostream &out)
{
    string labor_name = ENUM_KEY_STR(unit_labor, labor);
    out << labor_name << ": ";
    for (int i = 0; i < 20 - (int)labor_name.length(); i++)
        out << ' ';
    if (labor_infos[labor].mode() == DISABLE)
        out << "disabled" << endl;
    else
    {
        if (labor_infos[labor].mode() == HAULERS)
            out << "haulers, currently " << labor_infos[labor].active_dwarfs << " dwarfs" << endl;
    }
}

/**
 * This appears to be miscellaneous bookkeping stuff, if I am lucky I will not
 * have to edit it.
 */
command_result autohauler (color_ostream &out, std::vector <std::string> & parameters)
{
    CoreSuspender suspend;

    if (!Core::getInstance().isWorldLoaded()) {
        out.printerr("World is not loaded: please load a game first.\n");
        return CR_FAILURE;
    }

    if (parameters.size() == 1 &&
        (parameters[0] == "0" || parameters[0] == "enable" ||
         parameters[0] == "1" || parameters[0] == "disable"))
    {
        bool enable = (parameters[0] == "1" || parameters[0] == "enable");

        return plugin_enable(out, enable);
    }
    else if (parameters.size() >= 2 && parameters.size() <= 4)
    {
        if (!enable_autohauler)
        {
            out << "Error: The plugin is not enabled." << endl;
            return CR_FAILURE;
        }

        df::unit_labor labor = unit_labor::NONE;

        FOR_ENUM_ITEMS(unit_labor, test_labor)
        {
            if (parameters[0] == ENUM_KEY_STR(unit_labor, test_labor))
                labor = test_labor;
        }

        if (labor == unit_labor::NONE)
        {
            out.printerr("Could not find labor %s.\n", parameters[0].c_str());
            return CR_WRONG_USAGE;
        }

        if (parameters[1] == "haulers")
        {
            labor_infos[labor].set_mode(HAULERS);
            print_labor(labor, out);
            return CR_OK;
        }
        if (parameters[1] == "disable")
        {
            labor_infos[labor].set_mode(DISABLE);
            print_labor(labor, out);
            return CR_OK;
        }
        if (parameters[1] == "reset")
        {
            reset_labor(labor);
            print_labor(labor, out);
            return CR_OK;
        }

        print_labor(labor, out);

        return CR_OK;
    }
    else if (parameters.size() == 1 && parameters[0] == "reset-all")
    {
        if (!enable_autohauler)
        {
            out << "Error: The plugin is not enabled." << endl;
            return CR_FAILURE;
        }

        for (int i = 0; i < labor_infos.size(); i++)
        {
            reset_labor((df::unit_labor) i);
        }
        out << "All labors reset." << endl;
        return CR_OK;
    }
    else if (parameters.size() == 1 && parameters[0] == "list" || parameters[0] == "status")
    {
        if (!enable_autohauler)
        {
            out << "Error: The plugin is not enabled." << endl;
            return CR_FAILURE;
        }

        bool need_comma = 0;
        for (int i = 0; i < NUM_STATE; i++)
        {
            if (state_count[i] == 0)
                continue;
            if (need_comma)
                out << ", ";
            out << state_count[i] << ' ' << state_names[i];
            need_comma = 1;
        }
        out << endl;

        if (parameters[0] == "list")
        {
            FOR_ENUM_ITEMS(unit_labor, labor)
            {
                if (labor == unit_labor::NONE)
                    continue;

                print_labor(labor, out);
            }
        }

        return CR_OK;
    }
    else if (parameters.size() == 1 && parameters[0] == "debug")
    {
        if (!enable_autohauler)
        {
            out << "Error: The plugin is not enabled." << endl;
            return CR_FAILURE;
        }

        print_debug = 1;

        return CR_OK;
    }
    else
    {
        out.print("Automatically assigns hauling labors to dwarves.\n"
            "Activate with 'enable autohauler', deactivate with 'disable autohauler'.\n"
            "Current state: %d.\n", enable_autohauler);

        return CR_OK;
    }
}
