/**************************************************************************
 *  File: spells.c                                          Part of tbaMUD *
 *  Usage: Implementation of "manual spells."                              *
 *                                                                         *
 *  All rights reserved.  See license for complete information.            *
 *                                                                         *
 *  Copyright (C) 1993, 94 by the Trustees of the Johns Hopkins University *
 *  CircleMUD is based on DikuMUD, Copyright (C) 1990, 1991.               *
 **************************************************************************/

#include "conf.h"
#include "sysdep.h"
#include "structs.h"
#include "utils.h"
#include "comm.h"
#include "spells.h"
#include "handler.h"
#include "db.h"
#include "constants.h"
#include "interpreter.h"
#include "dg_scripts.h"
#include "act.h"
#include "fight.h"
#include "mud_event.h"
#include "house.h"  /* for house_can_enter() */
#include "screen.h" /* for QNRM, etc */
#include "craft.h"
#include "mudlim.h"
#include "string.h"
#include "item.h"
#include "domains_schools.h"

#define WALL_ITEM 101220
/* object values for walls */
#define WALL_TYPE   0 /* type, effect */
#define WALL_DIR    1 /* direction blocking */
#define WALL_LEVEL  2 /* level of wall in case creator can't be found */
#define WALL_IDNUM  3 /* creator's idnum */

#define PRISMATIC_SPHERE    90
#define WIZARD_EYE 	45

#define SUMMON_FAIL "You failed.\r\n"

/************************************************************/
/*  Functions, Events, etc needed to perform manual spells  */
/************************************************************/

/* Reference
#define SPELL_WALL_OF_FORCE             147
#define SPELL_WALL_OF_FIRE              282
#define SPELL_WALL_OF_THORNS            283
#define SPELL_WALL_OF_FOG               92
#define SPELL_PRISMATIC_SPHERE          211
| stops movement? | spellnum | long name | short name | keywords | duration |
   duration = 0 is default: 1 + level / 10
 */
struct wall_information wallinfo[] = {
  /* WALL_TYPE_FORCE 0 */
  { TRUE,
    SPELL_WALL_OF_FORCE,
    "\tRA wall of force stands towards the %s.\tn",
    "\tRa wall of force\tn",
    "wall force",
    1},
  /* WALL_TYPE_FIRE 1 */
  { FALSE,
    SPELL_WALL_OF_FIRE,
    "\trA wall of f\tRi\trre stands towards the %s.\tn",
    "\tra wall of fire\tn",
    "wall fire",
    0},
  /* WALL_TYPE_THORNS 2 */
  { FALSE,
    SPELL_WALL_OF_THORNS,
    "\tGA wall of thorns stands towards the %s.\tn",
    "\tGa wall of thorns\tn",
    "wall thorns",
    0},
  /* WALL_TYPE_FOG 3 */
  { FALSE,
    SPELL_WALL_OF_FOG,
    "\tCA foggy cloud forms a wall towards the %s.\tn",
    "\tCa wall of fog\tn",
    "wall fog",
    0},
  /* WALL_TYPE_PRISM 4 */
  { FALSE,
    SPELL_PRISMATIC_SPHERE,
    "\tnA \tRp\tYr\tBi\tMs\tWm\tn forms a wall towards the %s.\tn",
    "\tna wall of \tRp\tYr\tBi\tMs\tWm\tn",
    "wall prism",
    0},
};

/* called from movement, etc..  basically make the wall work - we will try
   to identify the wall creator in this function, if no creator is found,
   then we will use WALL_LEVEL plus the victim himself as the "creator" */
bool check_wall(struct char_data *victim, int dir) {
  struct obj_data *wall = NULL;
  struct char_data *ch = NULL;
  int level = 0;
  bool found_player = FALSE; /* you can pass through your own walls */
  int wall_spellnum = 0;
  int casttype = CAST_SPELL;

  for (wall = world[victim->in_room].contents; wall; wall = wall->next_content) {
    if (GET_OBJ_TYPE(wall) == ITEM_WALL && GET_OBJ_VAL(wall, WALL_DIR) == dir) {

      /* find the wall spellnum */
      wall_spellnum = wallinfo[GET_OBJ_VAL(wall, WALL_TYPE)].spell_num;

      /* lets see if we can find the wall creator! */
      ch = find_char(GET_OBJ_VAL(wall, WALL_IDNUM));
      if (!ch) {
        /* player probably logged out, so just use WALL_LEVEL to determine
         * damage */
        found_player = FALSE;
        level = GET_OBJ_VAL(wall, WALL_LEVEL);
      } else {
        level = GET_LEVEL(ch);
        found_player = TRUE;
      }

      /* if we found the player, we can also check if this victim is a friend! */
      if (found_player && !aoeOK(ch, victim, wall_spellnum))
        continue; /* pass safely through the wall */

      /* determine special damage, etc based on the WALL_TYPE */
      switch (GET_OBJ_VAL(wall, WALL_TYPE)) {
        default: /* default damage is 2d6 + level (above) */
          level += dice(2, 6);
          break;
      }

      if (CONFIG_PK_ALLOWED || (IS_NPC(victim) && !IS_PET(victim))) {
        /* we can add mag_effects, whatever we want here */

        /* the "creator" or caster of the spell was determined above */
        if (!found_player && mag_damage(level, victim, victim, NULL, wall_spellnum, 0, SAVING_FORT, casttype) < 0) {
          return TRUE; /* couldn't find the creator, victim died! */
        } else if (mag_damage(level, ch, victim, NULL, wall_spellnum, 0, SAVING_FORT, casttype) < 0) {
          return TRUE; /* he died! */
        }
      }

      if (wallinfo[GET_OBJ_VAL(wall, WALL_TYPE)].stops_movement) {
        act("You bump into $p.", FALSE, victim, wall, 0, TO_CHAR);
        act("$n bumps into $p.", FALSE, victim, wall, 0, TO_ROOM);
        return TRUE;
      }

    } /* end check to see if this is a valid wall */
  } /* end room search for walls */

  return FALSE;
}

/* this function will load the wall object, assign the appropriate values
   to it and do a little basic dummy checking */
void create_wall(struct char_data *ch, int room, int dir, int type, int level) {
  struct obj_data *wall = NULL;
  char buf[MAX_INPUT_LENGTH] = {'\0'};

  for (wall = world[room].contents; wall; wall = wall->next_content) {
    if (GET_OBJ_TYPE(wall) == ITEM_WALL && GET_OBJ_VAL(wall, WALL_DIR) == dir) {
      send_to_char(ch, "There is already a wall in that direction.\r\n");
      return;
    }
  }

  if (!CAN_GO(ch, dir)) {
    send_to_char(ch, "There is no open exit in that direction where you can put a wall in.\r\n");
    return;
  }

  wall = read_object(WALL_ITEM, VIRTUAL);
  if (!wall) { /* make sure we have the object */
    send_to_char(ch, "Please Report Wall Bug To Staff\r\n");
    return;
  }

  GET_OBJ_TYPE(wall) = ITEM_WALL; /* set type */
  wall->name = strdup(wallinfo[type].keyword); /* dump the keywords */
  wall->short_description = strdup(wallinfo[type].shortname); /* short descrip */

  /* create an item description */
  sprintf(buf, wallinfo[type].longname, dirs[dir]);
  wall->description = strdup(CAP(buf));

  /* either use a default time of 1 + level/10 or set duration */
  if (wallinfo[type].duration == 0)
    GET_OBJ_TIMER(wall) = 1 + level / 10;
  else
    GET_OBJ_TIMER(wall) = wallinfo[type].duration;

  /* make sure the wall fades eventually */
  if (!OBJ_FLAGGED(wall, ITEM_DECAY))
    TOGGLE_BIT_AR(GET_OBJ_EXTRA(wall), ITEM_DECAY);

  /* set the correct type, direction blocking, level and identifier */
  GET_OBJ_VAL(wall, WALL_TYPE) = type;
  GET_OBJ_VAL(wall, WALL_DIR) = dir;
  GET_OBJ_VAL(wall, WALL_LEVEL) = level; /* in case we can't find wall creator */
  GET_OBJ_VAL(wall, WALL_IDNUM) = GET_IDNUM(ch);

  /* all done!  drop the object in the room and let it wreak havoc! */
  obj_to_room(wall, room);
  sprintf(buf, "%s appears to the %s.\r\n", wallinfo[type].shortname, dirs[dir]);
  send_to_room(room, buf);
}

/* this function takes a real number for a room and returns:
   FALSE - mortals shouldn't be able to teleport to this destination
   TRUE - mortals CAN teleport to this destination
 * accepts NULL ch data
 * dim_lock means this test is checking for the dimensional lock affection
 */
int valid_mortal_tele_dest(struct char_data *ch, room_rnum dest, bool dim_lock) {

  if (dest == NOWHERE)
    return FALSE;

  /* if dim_lock is TRUE, we are checking for dim_lock, requires ch data */
  if (ch && IS_AFFECTED(ch, AFF_DIM_LOCK) && dim_lock)
    return FALSE;

  /* this function needs a vnum, not rnum */
  if (ch && !House_can_enter(ch, GET_ROOM_VNUM(dest)))
    return FALSE;

  if (ZONE_FLAGGED(GET_ROOM_ZONE(dest), ZONE_NOASTRAL))
    return FALSE;

  if (ROOM_FLAGGED(dest, ROOM_PRIVATE))
    return FALSE;

  if (ROOM_FLAGGED(dest, ROOM_DEATH))
    return FALSE;

  if (ROOM_FLAGGED(dest, ROOM_STAFFROOM))
    return FALSE;

  if (ZONE_FLAGGED(GET_ROOM_ZONE(dest), ZONE_CLOSED))
    return FALSE;

  if (ZONE_FLAGGED(GET_ROOM_ZONE(dest), ZONE_NOASTRAL))
    return FALSE;

  if (ROOM_FLAGGED(dest, ROOM_NOTELEPORT))
    return FALSE;

  //passed all tests!
  return TRUE;
}

/* Used by the locate object spell to check the alias list on objects */
int isname_obj(char *search, char *list) {
  char *found_in_list; /* But could be something like 'ring' in 'shimmering.' */
  char searchname[128];
  char namelist[MAX_STRING_LENGTH];
  int found_pos = -1;
  int found_name = 0; /* found the name we're looking for */
  int match = 1;
  int i;

  /* Force to lowercase for string comparisons */
  sprintf(searchname, "%s", search);
  for (i = 0; searchname[i]; i++)
    searchname[i] = LOWER(searchname[i]);

  sprintf(namelist, "%s", list);
  for (i = 0; namelist[i]; i++)
    namelist[i] = LOWER(namelist[i]);

  /* see if searchname exists any place within namelist */
  found_in_list = strstr(namelist, searchname);
  if (!found_in_list) {
    return 0;
  }

  /* Found the name in the list, now see if it's a valid hit. The following
   * avoids substrings (like ring in shimmering) is it at beginning of
   * namelist? */
  for (i = 0; searchname[i]; i++)
    if (searchname[i] != namelist[i])
      match = 0;

  if (match) /* It was found at the start of the namelist string. */
    found_name = 1;
  else { /* It is embedded inside namelist. Is it preceded by a space? */
    found_pos = found_in_list - namelist;
    if (namelist[found_pos - 1] == ' ')
      found_name = 1;
  }

  if (found_name)
    return 1;
  else
    return 0;
}

/* the main engine of charm spell, and similar */
void effect_charm(struct char_data *ch, struct char_data *victim,
        int spellnum, int casttype, int level) {
  struct affected_type af;
  int bonus = 0;

  /* resistance bonuses, etc */
  if (!IS_NPC(victim) && (GET_RACE(victim) == RACE_ELF || //elven enchantment resistance
          GET_RACE(victim) == RACE_H_ELF)) // added check for IS_NPC because RACE_TYPE_HUMAN == RACE_ELF and RACE_TYPE_ABERRATION == RACE_H_ELF
    bonus += 2;
  if (!IS_NPC(victim) && HAS_FEAT(victim, FEAT_STILL_MIND))
    bonus += 2;

  if (victim == ch)
    send_to_char(ch, "You like yourself even better!\r\n");

  else if (MOB_FLAGGED(victim, MOB_NOCHARM)) {
    send_to_char(ch, "Your victim doesn't seem vulnerable to this "
            "enchantments!\r\n");
    if (IS_NPC(victim))
      hit(victim, ch, TYPE_UNDEFINED, DAM_RESERVED_DBC, 0, FALSE);
  }
  else if (IS_AFFECTED(victim, AFF_MIND_BLANK)) {
    send_to_char(ch, "Your victim is protected from this "
            "enchantment!\r\n");
    if (IS_NPC(victim))
      hit(victim, ch, TYPE_UNDEFINED, DAM_RESERVED_DBC, 0, FALSE);
  }
  else if (AFF_FLAGGED(ch, AFF_CHARM))
    send_to_char(ch, "You can't have any followers of your own!\r\n");

  else if (AFF_FLAGGED(victim, AFF_CHARM))
    send_to_char(ch, "Your victim is already charmed.\r\n");

  else if (spellnum == SPELL_CHARM && (CASTER_LEVEL(ch) < GET_LEVEL(victim) ||
          GET_LEVEL(victim) >= 8))
    send_to_char(ch, "Your victim is too powerful.\r\n");

  else if (HAS_PET(ch))
    send_to_char(ch, "You can not manage more followers!\r\n");

  else if ((spellnum == SPELL_DOMINATE_PERSON || spellnum == SPELL_MASS_DOMINATION) &&
          CASTER_LEVEL(ch) < GET_LEVEL(victim))
    send_to_char(ch, "Your victim is too powerful.\r\n");

    /* player charming another player - no legal reason for this */
  else if (!CONFIG_PK_ALLOWED && !IS_NPC(victim))
    send_to_char(ch, "You fail - shouldn't be doing it anyway.\r\n");

  else if (circle_follow(victim, ch))
    send_to_char(ch, "Sorry, following in circles is not allowed.\r\n");

  else if (mag_resistance(ch, victim, 0)) {
    send_to_char(ch, "You failed to penetrate the spell resistance!");
    if (IS_NPC(victim))
      hit(victim, ch, TYPE_UNDEFINED, DAM_RESERVED_DBC, 0, FALSE);
  } else if (mag_savingthrow(ch, victim, SAVING_WILL, bonus, casttype, level, ENCHANTMENT)) {
    send_to_char(ch, "Your victim resists!\r\n");
    if (IS_NPC(victim))
      hit(victim, ch, TYPE_UNDEFINED, DAM_RESERVED_DBC, 0, FALSE);

  } else {
    /* slippery mind gives a second save */
    if (!IS_NPC(victim) && HAS_FEAT(victim, FEAT_SLIPPERY_MIND)) {
      send_to_char(victim, "\tW*Slippery Mind*\tn  ");
      if (mag_savingthrow(ch, victim, SAVING_WILL, 0, casttype, level, ENCHANTMENT)) {
        return;
      }
    }

    if (victim->master)
      stop_follower(victim);

    if (FIGHTING(ch) == victim)
      stop_fighting(ch);

    stop_fighting(victim);
    add_follower(victim, ch);

    new_affect(&af);
    if (spellnum == SPELL_CHARM)
      af.spell = SPELL_CHARM;
    if (spellnum == SPELL_CHARM_ANIMAL)
      af.spell = SPELL_CHARM_ANIMAL;
    else if (spellnum == SPELL_DOMINATE_PERSON)
      af.spell = SPELL_DOMINATE_PERSON;
    else if (spellnum == SPELL_MASS_DOMINATION)
      af.spell = SPELL_MASS_DOMINATION;
    af.duration = 100;
    if (GET_CHA_BONUS(ch))
      af.duration += GET_CHA_BONUS(ch) * 4;
    SET_BIT_AR(af.bitvector, AFF_CHARM);
    affect_to_char(victim, &af);

    act("Isn't $n just such a nice fellow?", FALSE, ch, 0, victim, TO_VICT);
    //    if (IS_NPC(victim))
    //      REMOVE_BIT_AR(MOB_FLAGS(victim), MOB_SPEC);
  }
  // should never get here
}


/* for dispel magic and greater dispelling */

/* a hack job so far, gets rid of the first x affections */

/* TODO:  add strength/etc to affection struct, that'd help a lot especially
   here */
void perform_dispel(struct char_data *ch, struct char_data *vict,
        struct obj_data *obj, int spellnum) {
  int i = 0, attempt = 0, challenge = 0, num_dispels = 0, msg = FALSE;

  // no target == room
  if (!vict && !obj) {
    if (IS_SET_AR(ROOM_FLAGS(IN_ROOM(ch)), (ROOM_FOG))) {

      //if (SECT(ch->in_room) != SECT_CLOUDS && SECT(ch->in_room) != SECT_SHADOWPLANE) {
      REMOVE_BIT_AR(ROOM_FLAGS(IN_ROOM(ch)), (ROOM_FOG));
      send_to_room(IN_ROOM(ch), "\tWThe fog dissipates into thin air!\tn\r\n");
      //} else {
      //send_to_room("Your magic is useless against these clouds!\r\n", ch->in_room);
      //}
    }
    return;
  }

  if (obj) {
    attempt = dice(1, 20) + CASTER_LEVEL(ch);
    challenge = dice(1, 20) + GET_OBJ_LEVEL(obj);

    if (GET_OBJ_TYPE(obj) == ITEM_WALL) {
      if (attempt >= challenge) {
        act("You dispel $p, which fades away.", FALSE, ch, obj, 0, TO_CHAR);
        act("$n dispels $p, which fades away.", FALSE, ch, obj, 0, TO_ROOM);
        extract_obj(obj);
      } else {
        act("You fail to dispel $p!", FALSE, ch, obj, NULL, TO_CHAR);
        act("$n fails to dispel $p!", FALSE, ch, obj, NULL, TO_ROOM);
      }
    }
    return;
  }

  if (vict == ch) {
    send_to_char(ch, "You dispel all your own magic!\r\n");
    act("$n dispels all $s magic!", FALSE, ch, 0, 0, TO_ROOM);
    if (ch->affected || AFF_FLAGS(ch)) {
      while (ch->affected) {
        if (spell_info[ch->affected->spell].wear_off_msg)
          send_to_char(ch, "%s\r\n",
                spell_info[ch->affected->spell].wear_off_msg);
        affect_remove(ch, ch->affected);
      }
      for (i = 0; i < AF_ARRAY_MAX; i++)
        AFF_FLAGS(ch)[i] = 0;
    }
    return;
  } else {
    attempt = dice(1, 20) + CASTER_LEVEL(ch);
    challenge = dice(1, 20) + CASTER_LEVEL(vict);

    if (spellnum == SPELL_GREATER_DISPELLING) {
      num_dispels = dice(2, 2);
      for (i = 0; i < num_dispels; i++) {
        if (attempt >= challenge) { //successful
          if (vict->affected) {
            msg = TRUE;
            affect_remove(vict, vict->affected);
          }
        }
        attempt = dice(1, 20) + CASTER_LEVEL(ch);
        challenge = dice(1, 20) + CASTER_LEVEL(vict);
      }
      if (msg) {
        send_to_char(ch, "You successfully dispel some magic!\r\n");
        act("$n dispels some of $N's magic!", FALSE, ch, 0, vict, TO_ROOM);
      } else {
        send_to_char(ch, "You fail your dispel magic attempt!\r\n");
        act("$n fails to dispel some of $N's magic!", FALSE, ch, 0, vict, TO_ROOM);
      }
      return;
    }

    if (spellnum == SPELL_DISPEL_MAGIC) {
      if (attempt >= challenge) { //successful
        send_to_char(ch, "You successfuly dispel some magic!\r\n");
        act("$n dispels some of $N's magic!", FALSE, ch, 0, vict, TO_ROOM);
        if (vict->affected)
          affect_remove(vict, vict->affected);
      } else { //failed
        send_to_char(ch, "You fail your dispel magic attempt!\r\n");
        act("$n fails to dispel some of $N's magic!", FALSE, ch, 0, vict, TO_ROOM);
      }
    }
  }
}

EVENTFUNC(event_ice_storm) {
  struct char_data *ch;
  struct mud_event_data *pMudEvent;

  if (event_obj == NULL)
    return 0;

  pMudEvent = (struct mud_event_data *) event_obj;
  ch = (struct char_data *) pMudEvent->pStruct;

  if (ch == NULL)
    return 0;

  call_magic(ch, NULL, NULL, SPELL_ICE_STORM, 0, CASTER_LEVEL(ch), CAST_SPELL);
  return 0;
}

EVENTFUNC(event_chain_lightning) {
  struct char_data *ch;
  struct mud_event_data *pMudEvent;

  if (event_obj == NULL)
    return 0;

  pMudEvent = (struct mud_event_data *) event_obj;
  ch = (struct char_data *) pMudEvent->pStruct;

  if (ch == NULL)
    return 0;

  call_magic(ch, NULL, NULL, SPELL_CHAIN_LIGHTNING, 0, CASTER_LEVEL(ch), CAST_SPELL);
  return 0;

}

/* The "return" of the event function is the time until the event is called
 * again. If we return 0, then the event is freed and removed from the list, but
 * any other numerical response will be the delay until the next call */
EVENTFUNC(event_acid_arrow) {
  struct char_data *ch, *victim = NULL;
  struct mud_event_data *pMudEvent;
  int casttype = CAST_SPELL;
  int level = 0;

  /* This is just a dummy check, but we'll do it anyway */
  if (event_obj == NULL)
    return 0;

  /* For the sake of simplicity, we will place the event data in easily
   * referenced pointers */
  pMudEvent = (struct mud_event_data *) event_obj;
  ch = (struct char_data *) pMudEvent->pStruct;
  if (ch && FIGHTING(ch)) //assign victim, if none escape
    victim = FIGHTING(ch);
  else
    return 0;

  if (ch == NULL || victim == NULL)
    return 0;
  if (ROOM_FLAGGED(IN_ROOM(ch), ROOM_PEACEFUL)) {
    send_to_char(ch, "This room just has such a peaceful, easy feeling...\r\n");
    return 0;
  }

  if (mag_resistance(ch, victim, 0))
    return 0;

  /* how about wands and everything else?? */
  level = CASTER_LEVEL(ch);
  if (level < 1)
    level = 15; /* so lame */

  if (mag_savingthrow(ch, victim, SAVING_REFL, 0, casttype, level, EVOCATION))
    damage(ch, victim, (dice(3, 6) / 2), SPELL_ACID_ARROW, DAM_ENERGY,
          FALSE);
  else
    damage(ch, victim, dice(3, 6), SPELL_ACID_ARROW, DAM_ENERGY,
          FALSE);

  update_pos(victim);
  return 0;
}

/* The "return" of the event function is the time until the event is called
 * again. If we return 0, then the event is freed and removed from the list, but
 * any other numerical response will be the delay until the next call */
EVENTFUNC(event_implode) {
  struct char_data *ch, *victim = NULL;
  struct mud_event_data *pMudEvent;
  int casttype = CAST_SPELL;
  int level = 0;

  /* This is just a dummy check, but we'll do it anyway */
  if (event_obj == NULL)
    return 0;

  /* For the sake of simplicity, we will place the event data in easily
   * referenced pointers */
  pMudEvent = (struct mud_event_data *) event_obj;
  ch = (struct char_data *) pMudEvent->pStruct;
  if (ch && FIGHTING(ch)) //assign victim, if none escape
    victim = FIGHTING(ch);
  else
    return 0;

  if (ch == NULL || victim == NULL)
    return 0;
  if (ROOM_FLAGGED(IN_ROOM(ch), ROOM_PEACEFUL)) {
    send_to_char(ch, "This room just has such a peaceful, easy feeling...\r\n");
    return 0;
  }

  if (mag_resistance(ch, victim, 0))
    return 0;

  /* how about wands and everything else?? */
  level = CASTER_LEVEL(ch);
  if (level < 1)
    level = 15; /* so lame */

  if (mag_savingthrow(ch, victim, SAVING_REFL, 0, casttype, level, DIVINATION))
    damage(ch, victim, (dice(CASTER_LEVEL(ch), 6) / 2), SPELL_IMPLODE, DAM_PUNCTURE,
          FALSE);
  else
    damage(ch, victim, dice(CASTER_LEVEL(ch), 6), SPELL_IMPLODE, DAM_PUNCTURE,
          FALSE);

  update_pos(victim);
  return 0;
}

/************************************************************/
/*  ASPELL defines                                          */

/************************************************************/


ASPELL(spell_acid_arrow) {
  int x = 0;

  if (ch == NULL || victim == NULL)
    return;
  if (ROOM_FLAGGED(IN_ROOM(ch), ROOM_PEACEFUL)) {
    send_to_char(ch, "This room just has such a peaceful, easy feeling...\r\n");
    return;
  }

  send_to_char(ch, "You send out an arrow of acid towards your opponent!\r\n");
  act("$n sends out an arrow of acid!", FALSE, ch, 0, 0, TO_ROOM);

  for (x = 0; x < (MAGIC_LEVEL(ch) / 3); x++) {
    NEW_EVENT(eACIDARROW, ch, NULL, ((x * 6) * PASSES_PER_SEC));
  }
}

ASPELL(spell_banish) {
  struct follow_type *k;

  if (!ch || !victim)
    return;

  /* go through target's list of followers */
  for (k = victim->followers; k; k = k->next) {
    /* follower in same room? */
    if (IN_ROOM(victim) == IN_ROOM(k->follower)) {
      /* actually a follower? */
      if (AFF_FLAGGED(k->follower, AFF_CHARM)) {
        /* might have to downgrade this */
        if (IS_NPC(k->follower)) {
          /* great, attempt to banish */
          act("$n banishes $N!", FALSE, ch, 0, k->follower, TO_ROOM);
          send_to_char(ch, "You banish %s!\r\n", GET_NAME(k->follower));
          extract_char(k->follower);

          /* 50% chance to keep on banishing away */
          if (!rand_number(0, 1))
            return;
        }
      }
    }
  }
}

ASPELL(spell_charm) // enchantment
{

  if (victim == NULL || ch == NULL)
    return;

  effect_charm(ch, victim, SPELL_CHARM, casttype, level);
}

ASPELL(spell_charm_animal) // enchantment
{
  if (victim == NULL || ch == NULL)
    return;

  if (IS_NPC(victim) && GET_RACE(victim) == RACE_TYPE_ANIMAL) {
    effect_charm(ch, victim, SPELL_CHARM_ANIMAL, casttype, level);
  } else {
    send_to_char(ch, "This spell can only be used on animals.");
  }
}

ASPELL(spell_clairvoyance) {
  room_rnum location, original_loc;

  if (ch == NULL || victim == NULL)
    return;

  if (AFF_FLAGGED(victim, AFF_NON_DETECTION)) {
    send_to_char(ch, "Your victim is affected by non-detection.\r\n");
    return;
  }

  if ((location = find_target_room(ch, GET_NAME(victim))) == NOWHERE) {
    send_to_char(ch, "Your spell fails.\r\n");
    return;
  }

  /* a location has been found. */
  original_loc = IN_ROOM(ch);
  char_from_room(ch);
  char_to_room(ch, location);
  look_at_room(ch, 0);

  /* check if the char is still there */
  if (IN_ROOM(ch) == location) {
    char_from_room(ch);
    char_to_room(ch, original_loc);
  }
}

ASPELL(spell_cloudkill) {
  if (INCENDIARY(ch) || DOOM(ch)) {
    send_to_char(ch, "You already have a cloud following you!\r\n");
    return;
  }
  send_to_char(ch, "You summon forth a cloud of death!\r\n");
  act("$n summons forth a cloud of death!", FALSE, ch, 0, 0, TO_ROOM);

  CLOUDKILL(ch) = MAGIC_LEVEL(ch) / 5;
}

ASPELL(spell_control_plants) {
  if (victim == NULL || ch == NULL)
    return;

  if (IS_NPC(victim) && GET_RACE(victim) == RACE_TYPE_PLANT) {
    effect_charm(ch, victim, SPELL_CONTROL_PLANTS, casttype, level);
  } else {
    send_to_char(ch, "This spell can only be used on plants.");
  }
}

/* i decided to wait for room events for this one */
ASPELL(spell_control_weather) {
  char arg[MAX_INPUT_LENGTH] = {'\0'};

  if (IS_NPC(ch) || !ch->desc)
    return;

  one_argument(cast_arg2, arg);

  if (is_abbrev(arg, "worsen")) {

  } else if (is_abbrev(arg, "improve")) {

  } else {
    send_to_char(ch, "You need to cast this spell with an argument of either, "
            "'worsen' or 'improve' in order for it to be a success!\r\n");
    return;
  }
}

ASPELL(spell_create_water) {
  int water;

  if (ch == NULL || obj == NULL)
    return;

  if (GET_OBJ_TYPE(obj) == ITEM_DRINKCON) {
    if ((GET_OBJ_VAL(obj, 2) != LIQ_WATER) && (GET_OBJ_VAL(obj, 1) != 0)) {
      name_from_drinkcon(obj);
      GET_OBJ_VAL(obj, 2) = LIQ_SLIME;
      name_to_drinkcon(obj, LIQ_SLIME);
    } else {
      water = MAX(GET_OBJ_VAL(obj, 0) - GET_OBJ_VAL(obj, 1), 0);
      if (water > 0) {
        if (GET_OBJ_VAL(obj, 1) >= 0)
          name_from_drinkcon(obj);
        GET_OBJ_VAL(obj, 2) = LIQ_WATER;
        GET_OBJ_VAL(obj, 1) += water;
        name_to_drinkcon(obj, LIQ_WATER);
        weight_change_object(obj, water);
        act("$p is filled.", FALSE, ch, obj, 0, TO_CHAR);
      }
    }
  }
}

ASPELL(spell_creeping_doom) {
  if (CLOUDKILL(ch) || INCENDIARY(ch)) {
    send_to_char(ch, "You already have a cloud following you!\r\n");
    return;
  }

  send_to_char(ch, "You summon forth a mass of centipede swarms!\r\n");
  act("$n summons forth a mass of centipede swarms!", FALSE, ch, 0, 0, TO_ROOM);

  DOOM(ch) = MAX(1, DIVINE_LEVEL(ch) / 4);
}

ASPELL(spell_detect_poison) {
  if (victim) {
    if (victim == ch) {
      if (AFF_FLAGGED(victim, AFF_POISON))
        send_to_char(ch, "You can sense poison in your blood.\r\n");
      else
        send_to_char(ch, "You feel healthy.\r\n");
    } else {
      if (AFF_FLAGGED(victim, AFF_POISON))
        act("You sense that $E is poisoned.", FALSE, ch, 0, victim, TO_CHAR);
      else
        act("You sense that $E is healthy.", FALSE, ch, 0, victim, TO_CHAR);
    }
  }

  if (obj) {
    switch (GET_OBJ_TYPE(obj)) {
      case ITEM_DRINKCON:
      case ITEM_FOUNTAIN:
      case ITEM_FOOD:
        if (GET_OBJ_VAL(obj, 3))
          act("You sense that $p has been contaminated.", FALSE, ch, obj, 0, TO_CHAR);
        else
          act("You sense that $p is safe for consumption.", FALSE, ch, obj, 0,
                TO_CHAR);
        break;
      default:
        send_to_char(ch, "You sense that it should not be consumed.\r\n");
    }
  }
}

ASPELL(spell_dismissal) {
  struct follow_type *k;

  if (!ch || !victim)
    return;

  /* go through target's list of followers */
  for (k = victim->followers; k; k = k->next) {
    /* follower in same room? */
    if (IN_ROOM(victim) == IN_ROOM(k->follower)) {
      /* actually a follower? */
      if (AFF_FLAGGED(k->follower, AFF_CHARM)) {
        /* has proper subrace to be dismissed? */
        if (IS_NPC(k->follower) &&
                HAS_SUBRACE(k->follower, SUBRACE_EXTRAPLANAR)) {
          /* great, attempt to dismiss and exit, just one victim */
          act("$n dismisses $N!", FALSE, ch, 0, k->follower, TO_ROOM);
          send_to_char(ch, "You dismiss %s!\r\n", GET_NAME(k->follower));
          extract_char(k->follower);
          return;
        }
      }
    }
  }
}

ASPELL(spell_dispel_magic) // divination
{

  if (ch == NULL)
    return;
  if (victim == NULL)
    victim = ch;

  perform_dispel(ch, victim, obj, SPELL_DISPEL_MAGIC);
}

ASPELL(spell_dominate_person) // enchantment
{
  if (victim == NULL || ch == NULL)
    return;

  effect_charm(ch, victim, SPELL_DOMINATE_PERSON, casttype, level);
}

/* Cannot use this spell on an equipped object or it will mess up the wielding
 * character's hit/dam totals. */
ASPELL(spell_enchant_weapon) // enchantment
{
  int i;

  if (ch == NULL || obj == NULL)
    return;

  /* Either already enchanted or not a weapon. */
  if (GET_OBJ_TYPE(obj) != ITEM_WEAPON || OBJ_FLAGGED(obj, ITEM_MAGIC))
    return;

  /* Make sure no other affections. */
  for (i = 0; i < MAX_OBJ_AFFECT; i++)
    if (obj->affected[i].location != APPLY_NONE)
      return;

  SET_BIT_AR(GET_OBJ_EXTRA(obj), ITEM_MAGIC);

  obj->affected[0].location = APPLY_HITROLL;
  obj->affected[0].modifier = MAX(1, (int) (MAGIC_LEVEL(ch) / 7));

  obj->affected[1].location = APPLY_DAMROLL;
  obj->affected[1].modifier = MAX(1, (int) (MAGIC_LEVEL(ch) / 7));

  if (IS_GOOD(ch)) {
    SET_BIT_AR(GET_OBJ_EXTRA(obj), ITEM_ANTI_EVIL);
    act("$p glows \tBblue\tn.", FALSE, ch, obj, 0, TO_CHAR);
  } else if (IS_EVIL(ch)) {
    SET_BIT_AR(GET_OBJ_EXTRA(obj), ITEM_ANTI_GOOD);
    act("$p glows \tRred\tn.", FALSE, ch, obj, 0, TO_CHAR);
  } else
    act("$p glows \tYyellow\tn.", FALSE, ch, obj, 0, TO_CHAR);
}

ASPELL(spell_greater_dispelling) // abjuration
{
  if (ch == NULL)
    return;
  if (victim == NULL)
    victim = ch;

  perform_dispel(ch, victim, obj, SPELL_GREATER_DISPELLING);
}

ASPELL(spell_group_summon) {
  struct char_data *tch = NULL;

  if (ch == NULL)
    return;

  if (!GROUP(ch))
    return;

  if (!valid_mortal_tele_dest(ch, IN_ROOM(ch), TRUE)) {
    send_to_char(ch, "A bright flash prevents your spell from working!");
    return;
  }

  while ((tch = (struct char_data *) simple_list(GROUP(ch)->members)) !=
          NULL) {

    if (ch == tch)
      continue;

    if (MOB_FLAGGED(tch, MOB_NOSUMMON))
      continue;

    if (IN_ROOM(tch) == IN_ROOM(ch))
      continue;

    if (!valid_mortal_tele_dest(tch, IN_ROOM(tch), TRUE))
      continue;

    act("$n disappears suddenly.", TRUE, tch, 0, 0, TO_ROOM);

    char_from_room(tch);
    char_to_room(tch, IN_ROOM(ch));

    act("$n arrives suddenly.", TRUE, tch, 0, 0, TO_ROOM);
    act("$n has summoned you!", FALSE, ch, 0, tch, TO_VICT);
    look_at_room(tch, 0);
    entry_memory_mtrigger(tch);
    greet_mtrigger(tch, -1);
    greet_memory_mtrigger(tch);
  }
}

ASPELL(spell_identify) // divination
{
  if (obj) {
    do_stat_object(ch, obj, ITEM_STAT_MODE_IDENTIFY_SPELL);

  } else if (victim) {
    /* victim */
    lore_id_vict(ch, victim);
  }
}

ASPELL(spell_implode) {
  int x = 0;

  if (ch == NULL || victim == NULL)
    return;
  if (ROOM_FLAGGED(IN_ROOM(ch), ROOM_PEACEFUL)) {
    send_to_char(ch, "This room just has such a peaceful, easy feeling...\r\n");
    return;
  }

  send_to_char(ch, "You cause %s to implode!\r\n", GET_NAME(victim));
  act("$n causes $N to implode!", FALSE, ch, 0, victim, TO_NOTVICT);
  act("$n causes you to implode!", FALSE, ch, 0, victim, TO_VICT);

  for (x = 0; x < (CASTER_LEVEL(ch) / 3); x++) {
    NEW_EVENT(eIMPLODE, ch, NULL, ((x * 6) * PASSES_PER_SEC));
  }
}

ASPELL(spell_incendiary_cloud) {
  if (CLOUDKILL(ch) || DOOM(ch)) {
    send_to_char(ch, "You already have a cloud following you!\r\n");
    return;
  }

  send_to_char(ch, "You summon forth an incendiary cloud!\r\n");
  act("$n summons forth an incendiary cloud!", FALSE, ch, 0, 0, TO_ROOM);

  INCENDIARY(ch) = MAX(1, MAGIC_LEVEL(ch) / 4);
}

ASPELL(spell_locate_creature) {
  struct char_data *i;
  int found = 0, num = 0;

  if (ch == NULL)
    return;
  if (victim == NULL)
    return;
  if (victim == ch) {
    send_to_char(ch, "You were once lost, but now you are found!\r\n");
    return;
  }

  send_to_char(ch, "%s\r\n", QNRM);
  for (i = character_list; i; i = i->next) {
    if (is_abbrev(GET_NAME(victim), GET_NAME(i)) && CAN_SEE(ch, i) && IN_ROOM(i) != NOWHERE) {
      found = 1;
      send_to_char(ch, "%3d. %-25s%s - %-25s%s", ++num, GET_NAME(i), QNRM,
              world[IN_ROOM(i)].name, QNRM);
      send_to_char(ch, "%s\r\n", QNRM);
    }
  }

  if (!found)
    send_to_char(ch, "Couldn't find any such creature.\r\n");
}

ASPELL(spell_locate_object) {
  struct obj_data *i;
  char name[MAX_INPUT_LENGTH];
  int j;

  if (!obj) {
    send_to_char(ch, "You sense nothing.\r\n");
    return;
  }

  /*  added a global var to catch 2nd arg. */
  sprintf(name, "%s", cast_arg2);

  j = CASTER_LEVEL(ch) / 2; /* # items to show = twice char's level */

  for (i = object_list; i && (j > 0); i = i->next) {
    if (!isname_obj(name, i->name))
      continue;

    send_to_char(ch, "%c%s", UPPER(*i->short_description), i->short_description + 1);

    if (i->carried_by)
      send_to_char(ch, " is being carried by %s.\r\n", PERS(i->carried_by, ch));
    else if (IN_ROOM(i) != NOWHERE)
      send_to_char(ch, " is in %s.\r\n", world[IN_ROOM(i)].name);
    else if (i->in_obj)
      send_to_char(ch, " is in %s.\r\n", i->in_obj->short_description);
    else if (i->worn_by)
      send_to_char(ch, " is being worn by %s.\r\n", PERS(i->worn_by, ch));
    else
      send_to_char(ch, "'s location is uncertain.\r\n");

    j--;
  }
}

ASPELL(spell_mass_domination) // enchantment
{
  struct char_data *tch, *next_tch;

  if (ch == NULL)
    return;

  for (tch = world[IN_ROOM(ch)].people; tch; tch = next_tch) {
    next_tch = tch->next_in_room;

    if (aoeOK(ch, tch, -1)) {
      effect_charm(ch, tch, SPELL_MASS_DOMINATION, casttype, level);
    }
  }
}

ASPELL(spell_plane_shift) {
  char arg[MAX_INPUT_LENGTH] = {'\0'};
  room_rnum to_room = NOWHERE;

  if (ch == NULL)
    return;

  if (!valid_mortal_tele_dest(ch, IN_ROOM(ch), TRUE)) {
    send_to_char(ch, "A bright flash prevents your spell from working!");
    return;
  }

  one_argument(cast_arg2, arg);

  if (is_abbrev(arg, "astral")) {

    if (ZONE_FLAGGED(GET_ROOM_ZONE(IN_ROOM(ch)), ZONE_ASTRAL_PLANE)) {
      send_to_char(ch, "You are already on the astral plane!\r\n");
      return;
    }

    do {
      to_room = rand_number(0, top_of_world);
    } while (!ZONE_FLAGGED(GET_ROOM_ZONE(to_room), ZONE_ASTRAL_PLANE));

  } else if (is_abbrev(arg, "ethereal")) {

    if (ZONE_FLAGGED(GET_ROOM_ZONE(IN_ROOM(ch)), ZONE_ETH_PLANE)) {
      send_to_char(ch, "You are already on the ethereal plane!\r\n");
      return;
    }

    do {
      to_room = rand_number(0, top_of_world);
    } while (!ZONE_FLAGGED(GET_ROOM_ZONE(to_room), ZONE_ETH_PLANE));

  } else if (is_abbrev(arg, "elemental")) {

    if (ZONE_FLAGGED(GET_ROOM_ZONE(IN_ROOM(ch)), ZONE_ELEMENTAL)) {
      send_to_char(ch, "You are already on the elemental plane!\r\n");
      return;
    }

    do {
      to_room = rand_number(0, top_of_world);
    } while (!ZONE_FLAGGED(GET_ROOM_ZONE(to_room), ZONE_ELEMENTAL));

  } else if (is_abbrev(arg, "prime")) {

    if (!ZONE_FLAGGED(GET_ROOM_ZONE(IN_ROOM(ch)), ZONE_ASTRAL_PLANE) &&
            !ZONE_FLAGGED(GET_ROOM_ZONE(IN_ROOM(ch)), ZONE_ETH_PLANE) &&
            !ZONE_FLAGGED(GET_ROOM_ZONE(IN_ROOM(ch)), ZONE_ELEMENTAL)
            ) {
      send_to_char(ch,
              "You need to be off the prime plane to gate to it!\r\n");
      return;
    }

    do {
      to_room = rand_number(0, top_of_world);
    } while ((ZONE_FLAGGED(GET_ROOM_ZONE(to_room), ZONE_ELEMENTAL) ||
            ZONE_FLAGGED(GET_ROOM_ZONE(to_room), ZONE_ETH_PLANE) ||
            ZONE_FLAGGED(GET_ROOM_ZONE(to_room), ZONE_ASTRAL_PLANE))
            );

  } else {
    send_to_char(ch, "Not a valid target (astral, ethereal, elemental, prime)");
    return;
  }

  if (!valid_mortal_tele_dest(ch, to_room, TRUE)) {
    send_to_char(ch, "A bright flash prevents your spell from working!");
    return;
  }

  send_to_char(ch, "You slowly fade out of existence...\r\n");
  act("$n slowly fades out of existence and is gone.",
          FALSE, ch, 0, 0, TO_ROOM);
  char_from_room(ch);
  char_to_room(ch, to_room);
  act("$n slowly fades into existence.", FALSE, ch, 0, 0, TO_ROOM);
  send_to_char(ch, "You slowly fade back into existence...\r\n");
  look_at_room(ch, 0);
  entry_memory_mtrigger(ch);
  greet_mtrigger(ch, -1);
  greet_memory_mtrigger(ch);
}

ASPELL(spell_polymorph) {
  char arg[MAX_INPUT_LENGTH] = {'\0'};

  if (IS_NPC(ch) || !ch->desc)
    return;

  one_argument(cast_arg2, arg);

  /* act.other.c, part of druid wildshape engine, the value "1" notifies the
       the function that this is the polymorph spells */
  wildshape_engine(ch, arg, 1);
}

ASPELL(spell_prismatic_sphere) {
  struct char_data *mob;

  if (AFF_FLAGGED(ch, AFF_CHARM))
    return;

  if (!(mob = read_mobile(PRISMATIC_SPHERE, VIRTUAL))) {
    send_to_char(ch, "You don't quite remember how to create that.\r\n");
    return;
  }

  char_to_room(mob, IN_ROOM(ch));
  IS_CARRYING_W(mob) = 0;
  IS_CARRYING_N(mob) = 0;

  act("$n conjures $N!", FALSE, ch, 0, mob, TO_ROOM);
  send_to_char(ch, "You conjure a prismatic sphere!\r\n");

  load_mtrigger(mob);
}

ASPELL(spell_recall) {
  if (victim == NULL || IS_NPC(victim))
    return;

  if (ROOM_FLAGGED(IN_ROOM(victim), ROOM_NOTELEPORT) ||
          ROOM_FLAGGED(IN_ROOM(victim), ROOM_NORECALL)) {
    send_to_char(ch, "Something in the area is hampering your magic!\r\n");
    return;
  }

  if (ZONE_FLAGGED(GET_ROOM_ZONE(IN_ROOM(victim)), ZONE_NOASTRAL)) {
    send_to_char(ch, "A bright flash prevents your spell from working!");
    return;
  }

  act("$n disappears.", TRUE, victim, 0, 0, TO_ROOM);
  char_from_room(victim);
  char_to_room(victim, r_mortal_start_room);
  act("$n appears in the middle of the room.", TRUE, victim, 0, 0, TO_ROOM);
  look_at_room(victim, 0);
  entry_memory_mtrigger(victim);
  greet_mtrigger(victim, -1);
  greet_memory_mtrigger(victim);
}

ASPELL(spell_refuge) // illusion (also divine)
{
  struct char_data *tch, *next_tch;
  struct affected_type af;

  if (ch == NULL)
    return;

  act("As $n makes a strange arcane gesture, a golden light descends\r\n"
          "from the heavens!\r\n", FALSE, ch, 0, 0, TO_ROOM);
  send_to_room(IN_ROOM(ch), "The room is a refuge!\r\n");

  for (tch = world[IN_ROOM(ch)].people; tch; tch = next_tch) {
    next_tch = tch->next_in_room;

    /* this is to possible victims */
    if (tch && aoeOK(ch, tch, -1)) {
      if (FIGHTING(tch)) {
        stop_fighting(tch);
        resetCastingData(tch);
      }
      if (IS_NPC(tch))
        clearMemory(tch);

      /* this should be allies */
    } else if (tch) {
      send_to_char(tch, "You are now refuged.\r\n");
      if (FIGHTING(tch))
        stop_fighting(tch);

      if (!AFF_FLAGGED(tch, AFF_SNEAK)) {
        SET_BIT_AR(AFF_FLAGS(tch), AFF_SNEAK);
      }
      if (!AFF_FLAGGED(tch, AFF_HIDE)) {
        SET_BIT_AR(AFF_FLAGS(tch), AFF_HIDE);
      }

      new_affect(&af);
      af.spell = SPELL_REFUGE;
      af.duration = 6;
      SET_BIT_AR(af.bitvector, AFF_REFUGE);
      affect_to_char(tch, &af);
    }
  }
}

ASPELL(spell_salvation) // divination
{
  room_vnum load_broom;

  if (ZONE_FLAGGED(GET_ROOM_ZONE(IN_ROOM(ch)), ZONE_ASTRAL_PLANE)) {
    send_to_char(ch, "You can't use salvation on the astral plane.\r\n");
    return;
  }
  if (ZONE_FLAGGED(GET_ROOM_ZONE(IN_ROOM(ch)), ZONE_ETH_PLANE)) {
    send_to_char(ch, "You can't use salvation on the ethereal plane.\r\n");
    return;
  }
  if (ZONE_FLAGGED(GET_ROOM_ZONE(IN_ROOM(ch)), ZONE_ELEMENTAL)) {
    send_to_char(ch, "You can't use salvation on the elemental plane.\r\n");
    return;
  }


  if (!PLR_FLAGGED(ch, PLR_SALVATION) ||
          !GET_SALVATION_NAME(ch) ||
          GET_SALVATION_ROOM(ch) == NOWHERE) {

    if (!valid_mortal_tele_dest(ch, real_room(world[ch->in_room].number), TRUE)) {
      send_to_char(ch, "You can't use salvation here.\r\n");
      return;
    }

    SET_BIT_AR(PLR_FLAGS(ch), PLR_SALVATION);
    load_broom = world[ch->in_room].number;
    if (GET_SALVATION_NAME(ch) != NULL)
      GET_SALVATION_NAME(ch) = NULL;
    GET_SALVATION_NAME(ch) = strdup(world[ch->in_room].name);
    GET_SALVATION_ROOM(ch) = load_broom;
    send_to_char(ch, "Your salvation is set to this room.\r\n");
    return;
  } else {
    if (!valid_mortal_tele_dest(ch, real_room(world[ch->in_room].number), TRUE)) {
      send_to_char(ch, "You can't use salvation here.\r\n");
      return;
    }

    REMOVE_BIT_AR(PLR_FLAGS(ch), PLR_SALVATION);
    if (GET_SALVATION_NAME(ch) != NULL)
      GET_SALVATION_NAME(ch) = NULL;
    load_broom = GET_SALVATION_ROOM(ch);
    load_broom = real_room(load_broom);
    act("$n disappears in a flash of white light", FALSE, ch, 0, 0, TO_ROOM);
    char_from_room(ch);
    char_to_room(ch, load_broom);
    send_to_char(ch, "As the flash of light disappears you can see the room.\r\n\r\n");
    act("$n appears in a flash of white light", FALSE, ch, 0, 0, TO_ROOM);
    look_at_room(ch, 0);
    return;
  }
}

ASPELL(spell_spellstaff) {
  char spellname[MAX_STRING_LENGTH] = {'\0'};
  struct obj_data *staff = NULL;
  int spellnum = 0;

  // cast_arg2 should be the spellname
  one_argument(cast_arg2, spellname);

  if (!*spellname || spellname == NULL) {
    send_to_char(ch, "You must specify which spell you want to enchant the staff with.\r\n");
    return;
  }

  // find staff in caster's hands
  if (GET_EQ(ch, WEAR_HOLD_1) && GET_OBJ_TYPE(GET_EQ(ch, WEAR_HOLD_1)) == ITEM_STAFF)
    staff = GET_EQ(ch, WEAR_HOLD_1);
  else if (GET_EQ(ch, WEAR_HOLD_2) && GET_OBJ_TYPE(GET_EQ(ch, WEAR_HOLD_2)) == ITEM_STAFF)
    staff = GET_EQ(ch, WEAR_HOLD_2);
  else if (GET_EQ(ch, WEAR_HOLD_2H) && GET_OBJ_TYPE(GET_EQ(ch, WEAR_HOLD_2H)) == ITEM_STAFF)
    staff = GET_EQ(ch, WEAR_HOLD_2H);

  //  for (staff = ch->carrying; staff; staff = staff->next_content) {
  //    if (GET_OBJ_TYPE(staff) == ITEM_STAFF) {
  //      // found one!
  //      break;
  //    }
  //  }

  if (staff) {
    if (GET_OBJ_VAL(staff, 2) > 0) {
      send_to_char(ch, "That staff is already enchanted with a spell.\r\n");
      return;
    } else {
      // determine the spellname to enchant with
      if (is_abbrev(spellname, "barkskin"))
        spellnum = SPELL_BARKSKIN;
      else if (is_abbrev(spellname, "cure light wounds"))
        spellnum = SPELL_CURE_LIGHT;
      else if (is_abbrev(spellname, "endurance"))
        spellnum = SPELL_ENDURANCE;
      else if (is_abbrev(spellname, "flame strike"))
        spellnum = SPELL_FLAME_STRIKE;
      else if (is_abbrev(spellname, "strength"))
        spellnum = SPELL_STRENGTH;

      if (spellnum != 0) {
        GET_OBJ_VAL(staff, 0) = GET_LEVEL(ch); // new staff only cast at caster's level
        GET_OBJ_VAL(staff, 2) = 1; // only good for 1 charge
        GET_OBJ_VAL(staff, 3) = spellnum;
        send_to_char(ch, "You enchant %s with the %s spell.\r\n", staff->short_description, spell_info[spellnum].name);
        act("$n concentrates on enhancing the power of $p.", FALSE, ch, staff, 0, TO_ROOM);
      } else {
        send_to_char(ch, "You are unable to store that spell in the staff.\r\n");
      }
    }
  } else {
    send_to_char(ch, "You are not holding a staff.\r\n");
  }
}

ASPELL(spell_storm_of_vengeance) {
  struct mud_event_data *pMudEvent = NULL;

  if (ch == NULL)
    return;

  if (ROOM_FLAGGED(IN_ROOM(ch), ROOM_PEACEFUL)) {
    send_to_char(ch, "This room just has such a peaceful, easy feeling...\r\n");
    return;
  }

  if ((pMudEvent = char_has_mud_event(ch, eICE_STORM))) {
    send_to_char(ch, "You already have a storm of vengeance!\r\n");
    return;
  }

  if ((pMudEvent = char_has_mud_event(ch, eCHAIN_LIGHTNING))) {
    send_to_char(ch, "You already have a storm of vengeance!\r\n");
    return;
  }

  send_to_char(ch, "You summon a storm of vengeance!\r\n");
  act("$n summons a storm of vengeance!", FALSE, ch, 0, 0, TO_ROOM);

  NEW_EVENT(eICE_STORM, ch, NULL, (6 * PASSES_PER_SEC));
  NEW_EVENT(eCHAIN_LIGHTNING, ch, NULL, (12 * PASSES_PER_SEC));
}

ASPELL(spell_summon) {
  if (ch == NULL || victim == NULL)
    return;

  if (GET_LEVEL(victim) > MIN(LVL_IMMORT - 1, level + 3)) {
    send_to_char(ch, "%s", SUMMON_FAIL);
    return;
  }

  if (!valid_mortal_tele_dest(victim, IN_ROOM(ch), TRUE)) {
    send_to_char(ch, "A bright flash prevents your spell from working!");
    return;
  }

  if (!valid_mortal_tele_dest(ch, IN_ROOM(victim), TRUE)) {
    send_to_char(ch, "A bright flash prevents your spell from working!");
    return;
  }

  if (ROOM_FLAGGED(IN_ROOM(ch), ROOM_NOSUMMON)) {
    send_to_char(ch, SUMMON_FAIL);
    return;
  }

  if (!CONFIG_PK_ALLOWED) {
    if (MOB_FLAGGED(victim, MOB_AGGRESSIVE)) {
      act("As the words escape your lips and $N travels\r\n"
              "through time and space towards you, you realize that $E is\r\n"
              "aggressive and might harm you, so you wisely send $M back.",
              FALSE, ch, 0, victim, TO_CHAR);
      return;
    }
    if (!IS_NPC(victim) && !PRF_FLAGGED(victim, PRF_SUMMONABLE) &&
            !PLR_FLAGGED(victim, PLR_KILLER)) {
      send_to_char(victim, "%s just tried to summon you to: %s.\r\n"
              "%s failed because you have summon protection on.\r\n"
              "Type NOSUMMON to allow other players to summon you.\r\n",
              GET_NAME(ch), world[IN_ROOM(ch)].name,
              (ch->player.sex == SEX_MALE) ? "He" : "She");

      send_to_char(ch, "You failed because %s has summon protection on.\r\n", GET_NAME(victim));
      mudlog(BRF, LVL_IMMORT, TRUE, "%s failed summoning %s to %s.", GET_NAME(ch), GET_NAME(victim), world[IN_ROOM(ch)].name);
      return;
    }
  }

  if (mag_resistance(ch, victim, 0))
    return;
  if (MOB_FLAGGED(victim, MOB_NOSUMMON)) {
    send_to_char(ch, "Your victim seems unsummonable.");
    return;
  }
  if (IS_NPC(victim) && mag_savingthrow(ch, victim, SAVING_WILL, 0, casttype, level, CONJURATION)) {
    send_to_char(ch, "%s", SUMMON_FAIL);
    return;
  }

  act("$n disappears suddenly.", TRUE, victim, 0, 0, TO_ROOM);

  char_from_room(victim);
  char_to_room(victim, IN_ROOM(ch));

  act("$n arrives suddenly.", TRUE, victim, 0, 0, TO_ROOM);
  act("$n has summoned you!", FALSE, ch, 0, victim, TO_VICT);
  look_at_room(victim, 0);
  entry_memory_mtrigger(victim);
  greet_mtrigger(victim, -1);
  greet_memory_mtrigger(victim);
}

ASPELL(spell_teleport) {
  room_rnum to_room = NOWHERE;

  if (ch == NULL)
    return;

  if (!victim) {
    victim = ch;
  }

  if (AFF_FLAGGED(victim, AFF_NOTELEPORT)) {
    send_to_char(ch, "Your spell fails to target that victim!\r\n");
    return;
  }

  to_room = IN_ROOM(victim);

  if (!valid_mortal_tele_dest(ch, to_room, TRUE)) {
    send_to_char(ch, "A bright flash prevents your spell from working!");
    return;
  }

  if (!valid_mortal_tele_dest(ch, IN_ROOM(ch), TRUE)) {
    send_to_char(ch, "A bright flash prevents your spell from working!");
    return;
  }

  /* no teleporting on the outter planes */
  if (ZONE_FLAGGED(GET_ROOM_ZONE(IN_ROOM(ch)), ZONE_ELEMENTAL) ||
          ZONE_FLAGGED(GET_ROOM_ZONE(IN_ROOM(ch)), ZONE_ETH_PLANE) ||
          ZONE_FLAGGED(GET_ROOM_ZONE(IN_ROOM(ch)), ZONE_ASTRAL_PLANE)
          ) {
    send_to_char(ch, "This magic won't help you travel on this plane!\r\n");
    return;
  }

  /* no teleporting off the prime plane to another */
  if (ZONE_FLAGGED(GET_ROOM_ZONE(to_room), ZONE_ELEMENTAL) ||
          ZONE_FLAGGED(GET_ROOM_ZONE(to_room), ZONE_ETH_PLANE) ||
          ZONE_FLAGGED(GET_ROOM_ZONE(to_room), ZONE_ASTRAL_PLANE)
          ) {
    send_to_char(ch, "Your target is beyond the reach of your magic!\r\n");
    return;
  }

  send_to_char(ch, "You slowly fade out of existence...\r\n");
  act("$n slowly fades out of existence and is gone.",
          FALSE, ch, 0, 0, TO_ROOM);
  char_from_room(ch);
  char_to_room(ch, to_room);
  act("$n slowly fades into existence.", FALSE, ch, 0, 0, TO_ROOM);
  send_to_char(ch, "You slowly fade back into existence...\r\n");
  look_at_room(ch, 0);
  entry_memory_mtrigger(ch);
  greet_mtrigger(ch, -1);
  greet_memory_mtrigger(ch);
}

ASPELL(spell_transport_via_plants) {
  obj_vnum obj_num = NOTHING;
  room_rnum to_room = NOWHERE;
  struct obj_data *dest_obj = NULL, *tmp_obj = NULL;

  if (ch == NULL)
    return;

  if (!obj) {
    send_to_char(ch, "Your target does not exist!\r\n");
    return;
  } else if (GET_OBJ_TYPE(obj) != ITEM_PLANT) {
    send_to_char(ch, "That is not a plant!\r\n");
    return;
  } else if (GET_OBJ_SIZE(obj) < SIZE_MEDIUM) {
    send_to_char(ch, "That plant is not large enough to transport you.\r\n");
    return;
  }
  obj_num = GET_OBJ_VNUM(obj);

  // find another of that plant in the world
  for (tmp_obj = object_list; tmp_obj; tmp_obj = tmp_obj->next) {
    if (tmp_obj == obj)
      continue;

    // we don't want to transport to a plant in someone's inventory
    if (GET_OBJ_VNUM(tmp_obj) == obj_num && !tmp_obj->carried_by) {
      dest_obj = tmp_obj;

      // 5% chance we will just stop at this obj
      if (!rand_number(0, 10))
        break;
    }
  }

  act("$n walks toward $p, and steps inside of it.", FALSE, ch, obj, 0, TO_ROOM);
  act("You walk toward $p, and step inside of it.", FALSE, ch, obj, 0, TO_CHAR);

  if (dest_obj != NULL) {
    to_room = dest_obj->in_room;
  }

  if (to_room == NOWHERE) {
    send_to_char(ch, "You are unable to find another exit, and are ejected from the plant.\r\n");
    act("$n comes tumbling out from inside of $p.", FALSE, ch, obj, 0, TO_ROOM);
    return;
  } else {
    if (!valid_mortal_tele_dest(ch, to_room, TRUE)) {
      send_to_char(ch, "A bright flash prevents your spell from working!\r\n");
      act("$n comes tumbling out from inside of $p.", FALSE, ch, obj, 0, TO_ROOM);
      return;
    }

    // transport player to new location
    char_from_room(ch);
    char_to_room(ch, to_room);
    look_at_room(ch, 0);
    act("You find your destination, and step out through $p.", FALSE, ch, dest_obj, 0, TO_CHAR);
    act("$n steps out from inside of $p!", FALSE, ch, dest_obj, 0, TO_ROOM);
    // TODO: make this an event, so player enters into the plant, and sees a couple messages, then comes out the other side
  }
}

ASPELL(spell_wall_of_thorns) {
  char arg[MAX_INPUT_LENGTH] = {'\0'};
  int dir = -1;

  if (AFF_FLAGGED(ch, AFF_CHARM))
    return;

  one_argument(cast_arg2, arg);
  if (!*arg) {
    send_to_char(ch, "You must specify a direction to conjure your wall at.\r\n");
    return;
  }

  dir = search_block(arg, dirs, FALSE);
  if (dir >= 0) {
    create_wall(ch, ch->in_room, dir, WALL_TYPE_THORNS, GET_LEVEL(ch));
  } else
    send_to_char(ch, "You must specify a direction to conjure your wall at.\r\n");
}

ASPELL(spell_wall_of_fire) {
  char arg[MAX_INPUT_LENGTH] = {'\0'};
  int dir = -1;

  if (AFF_FLAGGED(ch, AFF_CHARM))
    return;

  one_argument(cast_arg2, arg);
  if (!*arg) {
    send_to_char(ch, "You must specify a direction to conjure your wall at.\r\n");
    return;
  }

  dir = search_block(arg, dirs, FALSE);
  if (dir >= 0) {
    create_wall(ch, ch->in_room, dir, WALL_TYPE_FIRE, GET_LEVEL(ch));
  } else
    send_to_char(ch, "You must specify a direction to conjure your wall at.\r\n");
}

ASPELL(spell_wall_of_force) {
  char arg[MAX_INPUT_LENGTH] = {'\0'};
  int dir = -1;

  if (AFF_FLAGGED(ch, AFF_CHARM))
    return;

  one_argument(cast_arg2, arg);
  if (!*arg) {
    send_to_char(ch, "You must specify a direction to conjure your wall at.\r\n");
    return;
  }

  dir = search_block(arg, dirs, FALSE);
  if (dir >= 0) {
    create_wall(ch, ch->in_room, dir, WALL_TYPE_FORCE, GET_LEVEL(ch));
  } else
    send_to_char(ch, "You must specify a direction to conjure your wall at.\r\n");

  /* old wall of force */
  /*
  struct char_data *mob;
   *
  if (!(mob = read_mobile(WALL_OF_FORCE, VIRTUAL))) {
    send_to_char(ch, "You don't quite remember how to create that.\r\n");
    return;
  }

  char_to_room(mob, IN_ROOM(ch));
  IS_CARRYING_W(mob) = 0;
  IS_CARRYING_N(mob) = 0;

  act("$n conjures $N!", FALSE, ch, 0, mob, TO_ROOM);
  send_to_char(ch, "You conjure a wall of force!\r\n");

  load_mtrigger(mob);
   */
}

ASPELL(spell_wizard_eye) {
  struct char_data *eye = read_mobile(WIZARD_EYE, VIRTUAL);

  //dummy check
  if (!eye) {
    send_to_char(ch, "You don't quite remember how to create that.\r\n");
    return;
  }

  //first load the eye
  char_to_room(eye, IN_ROOM(ch));
  IS_CARRYING_W(eye) = 0;
  IS_CARRYING_N(eye) = 0;
  load_mtrigger(eye);

  //now take control
  send_to_char(ch, "You summon a wizard eye! (\tDType 'return' to return"
          " to your body\tn)\r\n");
  ch->desc->character = eye;
  ch->desc->original = ch;
  eye->desc = ch->desc;
  ch->desc = NULL;
}

#undef WIZARD_EYE
#undef PRISMATIC_SPHERE
#undef SUMMON_FAIL

#undef WALL_ITEM
#undef WALL_TYPE
#undef WALL_DIR
#undef WALL_LEVEL
#undef WALL_IDNUM
