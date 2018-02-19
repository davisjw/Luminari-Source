/* *************************************************************************
 *   File: spec_abilities.c                            Part of LuminariMUD *
 *  Usage: Code file for special abilities for weapons, armor and          *
 *         shields.                                                        *
 * Author: Ornir                                                           *
 ***************************************************************************
 *                                                                         *
 * In d20/Dungeons and Dragons, special abilities are what make magic      *
 * items -magical-.  These abilities, being wreathed in fire, exploding    *
 * with frost on a critical hit etc. are part of what defineds D&D.        *
 *                                                                         *
 * In order to implement these thing in LuminariMUD, some additions to the *
 * stock object model have been made (in structs.h).  These changes allow  *
 * the addition of any number of the defined special abilities to be added *
 * to the weapon, armor or shield in addition to any APPLY_ values that    *
 * the object has.  Additionally, an activation method must be defined.    *
 *                                                                         *
 * The code is defined similarly to the spells and commands in stock code, *
 * in that macros and an array of structures are used to define new        *
 * special abilities.                                                      *
 ***************************************************************************/

#include "conf.h"
#include "sysdep.h"
#include "structs.h"
#include "utils.h"
#include "comm.h"
#include "dg_event.h"
#include "spells.h"
#include "handler.h"
#include "db.h"
#include "interpreter.h"
#include "constants.h"
#include "dg_scripts.h"
#include "class.h"
#include "fight.h"
#include "utils.h"
#include "mud_event.h"
#include "act.h"  //perform_wildshapes
#include "mudlim.h"
#include "oasis.h"  // mob autoroller
#include "assign_wpn_armor.h"
#include "feats.h"
#include "race.h"
#include "spec_abilities.h"
#include "domains_schools.h"

/*
#include "conf.h"
#include "sysdep.h"
#include "structs.h"
#include "utils.h"
#include "fight.h"
#include "comm.h"
#include "structs.h"
#include "constants.h"
#include "dg_event.h"
#include "spells.h"
#include "spec_abilities.h"
#include "domains_schools.h"
*/

struct special_ability_info_type special_ability_info[NUM_SPECABS];

const char *unused_specabname = "!UNUSED!"; /* So we can get &unused_specabname */

const char *activation_methods[NUM_ACTIVATION_METHODS + 1] = {"None",
  "On Wear",
  "On Use",
  "Command Word",
  "On Hit",
  "On Crit",
  "\n"};

/* Procedures for loading and managing the special abilities on boot. */
static void add_weapon_special_ability(int specab, const char *name, int level, int actmtd, int targets, int violent, int time, int school, int cost, SPECAB_PROC_DEF(specab_proc)) {
  special_ability_info[specab].type = SPECAB_TYPE_WEAPON;
  special_ability_info[specab].level = level;
  special_ability_info[specab].activation_method = actmtd;
  special_ability_info[specab].targets = targets;
  special_ability_info[specab].violent = violent;
  special_ability_info[specab].name = name;
  special_ability_info[specab].time = time;
  special_ability_info[specab].school = school;
  special_ability_info[specab].cost = cost;
  special_ability_info[specab].special_ability_proc = specab_proc;
}


static void add_armor_special_ability(int specab, const char *name, int level, int actmtd, int targets, int violent, int time, int school, int cost, SPECAB_PROC_DEF(specab_proc)) {
  special_ability_info[specab].type = SPECAB_TYPE_ARMOR;
  special_ability_info[specab].level = level;
  special_ability_info[specab].activation_method = actmtd;
  special_ability_info[specab].targets = targets;
  special_ability_info[specab].violent = violent;
  special_ability_info[specab].name = name;
  special_ability_info[specab].time = time;
  special_ability_info[specab].school = school;
  special_ability_info[specab].cost = cost;
  special_ability_info[specab].special_ability_proc = specab_proc;
}

void daily_armor_specab(int specab, event_id event, int daily_uses) {
  special_ability_info[specab].daily_uses = daily_uses;  
  special_ability_info[specab].event = event;
}

static void add_unused_special_ability(int specab) {
  special_ability_info[specab].type = SPECAB_TYPE_NONE;
  special_ability_info[specab].level = 0;
  special_ability_info[specab].activation_method = 0;
  special_ability_info[specab].targets = 0;
  special_ability_info[specab].violent = 0;
  special_ability_info[specab].name = unused_specabname;
  special_ability_info[specab].time = 0;
  special_ability_info[specab].school = NOSCHOOL;
  special_ability_info[specab].cost = 0;
  special_ability_info[specab].daily_uses = 0;
  special_ability_info[specab].event = eNULL;
  special_ability_info[specab].special_ability_proc = NULL;
}

/**  (Targeting re-used from spells.h)
 **  TAR_IGNORE    : IGNORE TARGET.
 **  TAR_CHAR_ROOM : PC/NPC in room.
 **  TAR_CHAR_WORLD: PC/NPC in world.
 **  TAR_FIGHT_SELF: If fighting, and no argument, select tar_char as self.
 **  TAR_FIGHT_VICT: If fighting, and no argument, select tar_char as victim (fighting).
 **  TAR_SELF_ONLY : If no argument, select self, if argument check that it IS self.
 **  TAR_NOT_SELF  : Target is anyone else besides self.
 **  TAR_OBJ_INV   : Object in inventory.
 **  TAR_OBJ_ROOM  : Object in room.
 **  TAR_OBJ_WORLD : Object in world.
 **  TAR_OBJ_EQUIP : Object held.
 **/


void initialize_special_abilities(void) {
  int i;

  /* Initialize all abilities to UNUSED. */
  /* Do not change the loops below. */
  for (i = 0; i < NUM_SPECABS; i++)
    add_unused_special_ability(i);
  /* Do not change the loop above. */

  add_armor_special_ability(ARMOR_SPECAB_BLINDING, "Blinding", 7, ACTMTD_COMMAND_WORD,
          TAR_IGNORE, TRUE, 0, EVOCATION, 1, armor_specab_blinding);

  daily_armor_specab(ARMOR_SPECAB_BLINDING, eARMOR_SPECAB_BLINDING, 2);          

  add_weapon_special_ability(WEAPON_SPECAB_ANARCHIC, "Anarchic", 7, ACTMTD_NONE,
          TAR_IGNORE, FALSE, 0, EVOCATION, 2, NULL);

  add_weapon_special_ability(WEAPON_SPECAB_AXIOMATIC, "Axiomatic", 7, ACTMTD_NONE,
          TAR_IGNORE, FALSE, 0, EVOCATION, 2, NULL);

  add_weapon_special_ability(WEAPON_SPECAB_BANE, "Bane", 8, ACTMTD_ON_HIT | ACTMTD_ON_CRIT,
          TAR_FIGHT_VICT, FALSE, 0, CONJURATION, 1, weapon_specab_bane);

  add_weapon_special_ability(WEAPON_SPECAB_BRILLIANT_ENERGY, "Brilliant Energy", 16, ACTMTD_NONE,
          TAR_IGNORE, FALSE, 0, TRANSMUTATION, 4, NULL);

  add_weapon_special_ability(WEAPON_SPECAB_DANCING, "Dancing", 15, ACTMTD_NONE,
          TAR_IGNORE, FALSE, 0, TRANSMUTATION, 4, NULL);

  add_weapon_special_ability(WEAPON_SPECAB_DEFENDING, "Defending", 8, ACTMTD_NONE,
          TAR_IGNORE, FALSE, 0, ABJURATION, 1, NULL);

  add_weapon_special_ability(WEAPON_SPECAB_DISRUPTION, "Disruption", 14, ACTMTD_NONE,
          TAR_IGNORE, FALSE, 0, CONJURATION, 2, NULL);

  add_weapon_special_ability(WEAPON_SPECAB_DISTANCE, "Distance", 6, ACTMTD_NONE,
          TAR_IGNORE, FALSE, 0, DIVINATION, 1, NULL);

  add_weapon_special_ability(WEAPON_SPECAB_FLAMING, "Flaming", 10, ACTMTD_ON_HIT | ACTMTD_COMMAND_WORD,
          TAR_IGNORE, FALSE, 0, EVOCATION, 1, weapon_specab_flaming);

  add_weapon_special_ability(WEAPON_SPECAB_FLAMING_BURST, "Flaming Burst", 12, ACTMTD_ON_HIT | ACTMTD_ON_CRIT | ACTMTD_COMMAND_WORD,
          TAR_IGNORE, FALSE, 0, EVOCATION, 2, weapon_specab_flaming_burst);

  add_weapon_special_ability(WEAPON_SPECAB_FROST, "Frost", 8, ACTMTD_ON_HIT | ACTMTD_ON_CRIT | ACTMTD_COMMAND_WORD,
          TAR_IGNORE, FALSE, 0, EVOCATION, 1, weapon_specab_frost);

  add_weapon_special_ability(WEAPON_SPECAB_GHOST_TOUCH, "Ghost Touch", 9, ACTMTD_NONE,
          TAR_IGNORE, FALSE, 0, CONJURATION, 1, NULL);

  add_weapon_special_ability(WEAPON_SPECAB_HOLY, "Holy", 7, ACTMTD_NONE,
          TAR_IGNORE, FALSE, 0, EVOCATION, 2, NULL);

  add_weapon_special_ability(WEAPON_SPECAB_ICY_BURST, "Icy Burst", 10, ACTMTD_NONE,
          TAR_IGNORE, FALSE, 0, EVOCATION, 2, NULL);

  add_weapon_special_ability(WEAPON_SPECAB_KEEN, "Keen", 10, ACTMTD_NONE,
          TAR_IGNORE, FALSE, 0, TRANSMUTATION, 1, NULL);

  add_weapon_special_ability(WEAPON_SPECAB_KI_FOCUS, "Ki Focus", 8, ACTMTD_NONE,
          TAR_IGNORE, FALSE, 0, TRANSMUTATION, 1, NULL);

  add_weapon_special_ability(WEAPON_SPECAB_MERCIFUL, "Merciful", 5, ACTMTD_NONE,
          TAR_IGNORE, FALSE, 0, CONJURATION, 1, NULL);

  add_weapon_special_ability(WEAPON_SPECAB_MIGHTY_CLEAVING, "Mighty Cleaving", 8, ACTMTD_NONE,
          TAR_IGNORE, FALSE, 0, EVOCATION, 1, NULL);

  add_weapon_special_ability(WEAPON_SPECAB_RETURNING, "Returning", 7, ACTMTD_NONE,
          TAR_IGNORE, FALSE, 0, TRANSMUTATION, 1, NULL);

  add_weapon_special_ability(WEAPON_SPECAB_SEEKING, "Seeking", 12, ACTMTD_NONE,
          TAR_IGNORE, FALSE, 0, DIVINATION, 1, NULL);

  add_weapon_special_ability(WEAPON_SPECAB_SHOCK, "Shock", 8, ACTMTD_NONE,
          TAR_IGNORE, FALSE, 0, EVOCATION, 1, NULL);

  add_weapon_special_ability(WEAPON_SPECAB_SHOCKING_BURST, "Shocking Burst", 9, ACTMTD_NONE,
          TAR_IGNORE, FALSE, 0, EVOCATION, 2, NULL);

  add_weapon_special_ability(WEAPON_SPECAB_SPEED, "Speed", 7, ACTMTD_NONE,
          TAR_IGNORE, FALSE, 0, TRANSMUTATION, 3, NULL);

  add_weapon_special_ability(WEAPON_SPECAB_SPELL_STORING, "Spell Storing", 12, ACTMTD_NONE,
          TAR_IGNORE, FALSE, 0, EVOCATION, 1, NULL);

  add_weapon_special_ability(WEAPON_SPECAB_THUNDERING, "Thundering", 5, ACTMTD_NONE,
          TAR_IGNORE, FALSE, 0, NECROMANCY, 1, NULL);

  add_weapon_special_ability(WEAPON_SPECAB_THROWING, "Throwing", 5, ACTMTD_NONE,
          TAR_IGNORE, FALSE, 0, TRANSMUTATION, 1, NULL);

  add_weapon_special_ability(WEAPON_SPECAB_UNHOLY, "Unholy", 7, ACTMTD_NONE,
          TAR_IGNORE, FALSE, 0, EVOCATION, 2, NULL);

  add_weapon_special_ability(WEAPON_SPECAB_VICIOUS, "Vicious", 9, ACTMTD_NONE,
          TAR_IGNORE, FALSE, 0, NECROMANCY, 1, NULL);

  add_weapon_special_ability(WEAPON_SPECAB_VORPAL, "Vorpal", 18, ACTMTD_NONE,
          TAR_IGNORE, FALSE, 0, NECROMANCY /* TRANSMUTATION TOO */, 5, NULL);

  add_weapon_special_ability(WEAPON_SPECAB_WOUNDING, "Wounding", 10, ACTMTD_NONE,
          TAR_IGNORE, FALSE, 0, EVOCATION, 2, NULL);

}

bool obj_has_special_ability(struct obj_data *obj, int ability) {
  struct obj_special_ability *specab = NULL;

  for (specab = obj->special_abilities; specab != NULL; specab = specab->next) {
    if (specab->ability == ability)
      return TRUE;
  }

  return FALSE;
}

struct obj_special_ability* get_obj_special_ability(struct obj_data *obj, int ability) {
  struct obj_special_ability *specab = NULL;

  for (specab = obj->special_abilities; specab != NULL; specab = specab->next) {
    if (specab->ability == ability)
      return specab;
  }

  return NULL;

}

/* Returns the number of activated abilites. */
int process_weapon_abilities(struct obj_data *weapon, /* The weapon to check for special abilities. */
        struct char_data *ch, /* The wielder of the weapon. */
        struct char_data *victim, /* The target of the ability (either fighting or
							 * specified explicitly. */
        int actmtd, /* Activation method */
        char *cmdword) /* Command word (optional, NULL if none. */
 {
  int activated_abilities = 0;
  struct obj_special_ability *specab; /* struct for iterating through the object's abilities. */
  /* Run the 'callbacks' for each of the special abilities on weapon that match the activation method. */
  for (specab = weapon->special_abilities; specab != NULL; specab = specab->next) {
    /* Only deal with weapon special abilities */
    if (special_ability_info[specab->ability].type != SPECAB_TYPE_WEAPON)
      continue;
    /* So we have an ability, check the activation method. */
    if (IS_SET(specab->activation_method, actmtd)) { /* Match! */
      if (actmtd == ACTMTD_COMMAND_WORD) { /* check the command word */
        if (strcmp(specab->command_word, cmdword)) /* No Match */
          continue; /* Skip this ability, no match. */
      }
      if (special_ability_info[specab->ability].special_ability_proc == NULL) {
        log("SYSERR: PROCESS_WEAPON_ABILITIES: ability '%s' has no callback function!", special_ability_info[specab->ability].name);
        continue;
      }
      activated_abilities++;
      (*special_ability_info[specab->ability].special_ability_proc) (specab, weapon, ch, victim, actmtd);

    }
  }

  return activated_abilities;
}

int process_armor_abilities(struct char_data *ch, /* The player wearing the armor. */
                            struct char_data *victim, /* The target of the ability (either fighting or specified explicitly. */
                            int actmtd, /* Activation method */
                            char *cmdword) /* Command word (optional, NULL if none. */
 {
  int i = 0;
  int activated_abilities = 0;
  struct obj_data *obj;
  
  /* Check every piece of armor/equipment that the player is wearing. */
  for (i = 0; i < NUM_WEARS; i++) {
    
    if ((i == WEAR_WIELD_1) ||
        (i == WEAR_WIELD_OFFHAND) ||
        (i == WEAR_WIELD_2H)) {
      /* Skip weapons */
      continue;
    } 

    obj = GET_EQ(ch, i);
    if (obj != NULL) {
      struct obj_special_ability *specab; /* struct for iterating through the object's abilities. */
      /* Run the 'callbacks' for each of the special abilities on the object that match the activation method. */
      for (specab = obj->special_abilities; specab != NULL; specab = specab->next) {
        /* Only deal with armor special abilities */
        if (special_ability_info[specab->ability].type != SPECAB_TYPE_ARMOR)
          continue;
        
        /* So we have an ability, check the activation method. */
        if (IS_SET(specab->activation_method, actmtd)) { /* Match! */
          if (actmtd == ACTMTD_COMMAND_WORD) { /* check the command word */
            if (strcmp(specab->command_word, cmdword)) /* No Match */
              continue; /* Skip this ability, no match. */
          }
          if (special_ability_info[specab->ability].special_ability_proc == NULL) {
            log("SYSERR: PROCESS_ARMOR_ABILITIES: ability '%s' has no callback function!", special_ability_info[specab->ability].name);
            continue;
          }
          activated_abilities++;
          (*special_ability_info[specab->ability].special_ability_proc) (specab, obj, ch, victim, actmtd);

        }
      }
    }
  }
  return activated_abilities;
}

ARMOR_SPECIAL_ABILITY(armor_specab_blinding) {
  /*
   * level
   * armor
   * ch
   * victim
   * obj
   */
  struct char_data *tch = NULL;
  struct list_data *room_list = NULL;

  switch (actmtd) {
    case ACTMTD_COMMAND_WORD: /* User UTTERs the command word. */    
      /* Activate the blinding ability.
       *  - Check the cooldown - This ability can be used 2x a day, so set a cooldown on the shield using events.
       *  - Send a message to the room, then attempt to blind engaged creatures.
       */      
      if(daily_armor_specab_uses_remaining(armor, ARMOR_SPECAB_BLINDING) == 0) {
        /* No uses remaining... */
        send_to_char(ch, "The item must regain its energies before this ability can be invoked again.\r\n");
        break;
      }

      /* When using a list, we have to make sure to allocate the list as it
       * uses dynamic memory */
      room_list = create_list();

      /* We search through the "next_in_room", and grab all NPCs fighting ch and add them
       * to our list */
      if (!IN_ROOM(ch))
        break;

      for (tch = world[IN_ROOM(ch)].people; tch; tch = tch->next_in_room)
        if (FIGHTING(tch) == ch)
          add_to_list(tch, room_list);

      /* If our list is empty or has "0" entries print a message and enable coolodown. */
      if (room_list->iSize == 0) {        
        send_to_char(ch, "There are no enemies engaged in combat wih you!\r\n");        
      } else {      
        /* Find all engaged opponents (in the room), give them a chance to avoid getting blinded, blind the unlucky ones. */
        send_to_char(ch, "This will have been a short (1d4) blind attack!  Rawr!\r\n");
      }

      /* Now that our attack is done, let's free our list */
      if (room_list)
        free_list(room_list);
   
      start_armor_specab_daily_use_cooldown(armor, ARMOR_SPECAB_BLINDING);
      break;
    case ACTMTD_USE: /* User USEs the item. */
      break;
    case ACTMTD_ON_HIT: /* Called whenever a weapon hits an enemy. */
      break;
    case ACTMTD_ON_CRIT: /* Called whenever a weapon hits critically. */
    case ACTMTD_WEAR: /* Called whenever the item is worn. */
    default:
      /* Do nothing. */
      break;
  }
}

WEAPON_SPECIAL_ABILITY(weapon_specab_flaming) {
  /*
   * level
   * weapon
   * ch
   * victim
   * obj
   */
  switch (actmtd) {
    case ACTMTD_COMMAND_WORD: /* User UTTERs the command word. */
    case ACTMTD_USE: /* User USEs the item. */
      /* Activate the flaming ability.
       *  - Set the FLAMING bit on the weapon (this affects the display,
       *    and is used to toggle the effect.)
       */
      if (OBJ_FLAGGED(weapon, ITEM_FLAMING)) {
        /* Flaming is on, turn it off. */
        send_to_char(ch, "The magical flames wreathing your weapon vanish.\r\n");
        act("The magical flames wreathing $n's $o vanish.", FALSE, ch, weapon, NULL, TO_ROOM);

        REMOVE_OBJ_FLAG(weapon, ITEM_FLAMING);
      } else {
        /* FLAME ON! */
        send_to_char(ch, "Magical flames spread down the length of your weapon!\r\n");
        act("Magical flames spread down the length of $n's $o!", FALSE, ch, weapon, NULL, TO_ROOM);

        SET_OBJ_FLAG(weapon, ITEM_FLAMING);
      }
      break;
    case ACTMTD_ON_HIT: /* Called whenever a weapon hits an enemy. */
      if (OBJ_FLAGGED(weapon, ITEM_FLAMING)) /* Burn 'em. */
        if (victim) {
          damage(ch, victim, dice(1, 6), TYPE_SPECAB_FLAMING, DAM_FIRE, FALSE);
        }
      break;
    case ACTMTD_ON_CRIT: /* Called whenever a weapon hits critically. */
    case ACTMTD_WEAR: /* Called whenever the item is worn. */
    default:
      /* Do nothing. */
      break;
  }
}

/* A weapon with Flaming burst functions as a flaming weapon, except on critical hits it
 * performs a flame burst for 1d10 extra damage. */
WEAPON_SPECIAL_ABILITY(weapon_specab_flaming_burst) {
  /*
   * level
   * weapon
   * ch
   * victim
   * obj
   */
  switch (actmtd) {
    case ACTMTD_COMMAND_WORD: /* User UTTERs the command word. */
    case ACTMTD_USE: /* User USEs the item. */
      /* Activate the flaming ability.
       *  - Set the FLAMING bit on the weapon (this affects the display,
       *    and is used to toggle the effect.)
       */
      if (OBJ_FLAGGED(weapon, ITEM_FLAMING)) {
        /* Flaming is on, turn it off. */
        send_to_char(ch, "The magical flames wreathing your weapon vanish.\r\n");
        act("The magical flames wreathing $n's $o vanish.", FALSE, ch, weapon, NULL, TO_ROOM);

        REMOVE_OBJ_FLAG(weapon, ITEM_FLAMING);
      } else {
        /* FLAME ON! */
        send_to_char(ch, "Magical flames spread down the length of your weapon!\r\n");
        act("Magical flames spread down the length of $n's $o!", FALSE, ch, weapon, NULL, TO_ROOM);

        SET_OBJ_FLAG(weapon, ITEM_FLAMING);
      }
      break;
    case ACTMTD_ON_HIT: /* Called whenever a weapon hits an enemy. */
      if (OBJ_FLAGGED(weapon, ITEM_FLAMING)) /* Burn 'em. */
        if (victim) {
          /*send_to_char(ch, "\tr[spcab]\tn");*/
          damage(ch, victim, dice(1, 6), TYPE_SPECAB_FLAMING, DAM_FIRE, FALSE);
        }
      break;
    case ACTMTD_ON_CRIT: /* Called whenever a weapon hits critically. */
      /* We don't care if the flaming property is active, it bursts anyway! */
      if (victim) {
        /* send_to_char(ch,"\tr[burst]\tn");*/
        damage(ch, victim, dice(1, 10), TYPE_SPECAB_FLAMING_BURST, DAM_FIRE, FALSE);
      }
      break;
    case ACTMTD_WEAR: /* Called whenever the item is worn. */
    default:
      /* Do nothing. */
      break;
  }
}

/* A weapon wne prints messages when fighting it's favored enemy... */
WEAPON_SPECIAL_ABILITY(weapon_specab_bane) {
  /*
   * level
   * weapon
   * ch
   * victim
   * obj
   */
  switch (actmtd) {
    case ACTMTD_ON_HIT: /* Called whenever a weapon hits an enemy. */
      if ((dice(1, 6) > 4) && ((GET_RACE(victim) == specab->value[0]) && (HAS_SUBRACE(victim, specab->value[1])))) {

        act("Your $o hums happily as you fight $N!", FALSE, ch, weapon, victim, TO_CHAR);
        act("$n's $o hums happily as $e fights you!", FALSE, ch, weapon, victim, TO_VICT);
        act("$n's $o hums happily as $e fights $N!", FALSE, ch, weapon, victim, TO_NOTVICT);
      }
      break;
    case ACTMTD_ON_CRIT: /* Called whenever a weapon hits critically. */
      if ((GET_RACE(victim) == specab->value[0]) && (HAS_SUBRACE(victim, specab->value[1]))) {
        act("Waves of pleasure course into you from your $o as you strike $N!", FALSE, ch, weapon, victim, TO_CHAR);
      }
      break;
    default:
      /* Do nothing. */
      break;
  }
}

/* A weapon with the frost special ability generates cold, becoming encrusted with frost and dealing
 * cold damage on a regular hit. */
WEAPON_SPECIAL_ABILITY(weapon_specab_frost) {
  /*
   * level
   * weapon
   * ch
   * victim
   * obj
   */
  switch (actmtd) {
    case ACTMTD_COMMAND_WORD: /* User UTTERs the command word. */
    case ACTMTD_USE: /* User USEs the item. */
      /* Activate the flaming ability.
       *  - Set the FROST bit on the weapon (this affects the display,
       *    and is used to toggle the effect.)
       */
      if (OBJ_FLAGGED(weapon, ITEM_FROST)) {
        /* Flaming is on, turn it off. */
        send_to_char(ch, "The magical frost sheathing your weapon vanishes.\r\n");
        act("The magical frost sheathing $n's $o vanishes.", FALSE, ch, weapon, NULL, TO_ROOM);

        REMOVE_OBJ_FLAG(weapon, ITEM_FROST);
      } else {
        /* FROST ON! */
        send_to_char(ch, "Magical frost spreads down the length of your weapon!\r\n");
        act("Magical frost spreads down the length of $n's $o!", FALSE, ch, weapon, NULL, TO_ROOM);

        SET_OBJ_FLAG(weapon, ITEM_FROST);
      }
      break;
    case ACTMTD_ON_HIT: /* Called whenever a weapon hits an enemy. */
      if (OBJ_FLAGGED(weapon, ITEM_FROST)) /* Freeze 'em. */
        if (victim) {
          damage(ch, victim, dice(1, 6), TYPE_SPECAB_FROST, DAM_COLD, FALSE);
        }
      break;
    case ACTMTD_ON_CRIT: /* Called whenever a weapon hits critically. */
    case ACTMTD_WEAR: /* Called whenever the item is worn. */
    default:
      /* Do nothing. */
      break;
  }
}

int add_draconic_claws_elemental_damage(struct char_data *ch, struct char_data *victim)
{
  int dam = dice(1, 6);
  int damtype = draconic_heritage_energy_types[GET_BLOODLINE_SUBTYPE(ch)];
  dam -= compute_damtype_reduction(ch, damtype);
  return MAX(0, dam);
}