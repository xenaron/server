//////////////////////////////////////////////////////////////////////
// OpenTibia - an opensource roleplaying game
//////////////////////////////////////////////////////////////////////
// class representing the gamestate
//////////////////////////////////////////////////////////////////////
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
// 
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software Foundation,
// Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
//////////////////////////////////////////////////////////////////////


#include "definitions.h"

#include <string>
#include <sstream>

#include <map>
//#include <algorithm>

#ifdef __DEBUG_CRITICALSECTION__
#include <iostream>
#include <fstream>
#endif

#include <boost/config.hpp>
#include <boost/bind.hpp>

using namespace std;

#include <stdio.h>
#include "otsystem.h"
#include "items.h"
#include "commands.h"
#include "creature.h"
#include "player.h"
#include "monster.h"
#include "npc.h"
#include "game.h"
#include "tile.h"

#include "spells.h"
#include "actions.h"
#include "ioplayer.h"
#include "chat.h"

#include "luascript.h"
#include <ctype.h>

#if defined __EXCEPTION_TRACER__
#include "exception.h"
extern OTSYS_THREAD_LOCKVAR maploadlock;
#endif

#define EVENT_CHECKCREATURE          123
#define EVENT_CHECKCREATUREATTACKING 124

extern LuaScript g_config;
extern Spells spells;
extern Actions actions;
extern Commands commands;
extern Chat g_chat;

extern std::vector< std::pair<unsigned long, unsigned long> > bannedIPs;

//

/*
GameState::GameState(Game *game, const Range &range)
{
	this->game = game;
	game->getSpectators(range, spectatorlist);
}

void GameState::onAttack(Creature* attacker, const Position& pos, const MagicEffectClass* me)
{
	//Tile *tile = game->map->getTile(pos.x, pos.y, pos.z);
	Tile *tile = game->map->getTile(pos);

	if(!tile)
		return;

	CreatureVector::iterator cit;
	Player* attackPlayer = dynamic_cast<Player*>(attacker);
	Creature *targetCreature = NULL;
	Player *targetPlayer = NULL;
	for(cit = tile->creatures.begin(); cit != tile->creatures.end(); ++cit) {
		targetCreature = (*cit);
		targetPlayer = dynamic_cast<Player*>(targetCreature);

		int damage = me->getDamage(targetCreature, attacker);
		int manaDamage = 0;
		
		if (damage > 0) {
			if(attackPlayer && attackPlayer->access == 0) {
				if(targetPlayer && targetPlayer != attackPlayer && game->getWorldType() != WORLD_TYPE_NO_PVP)
					attackPlayer->pzLocked = true;
			}

			if(targetCreature->access == 0 && targetPlayer && game->getWorldType() != WORLD_TYPE_NO_PVP) {
				targetPlayer->inFightTicks = (long)g_config.getGlobalNumber("pzlocked", 0);
				targetPlayer->sendIcons();
			}

			if(game->getWorldType() == WORLD_TYPE_NO_PVP && attackPlayer && targetPlayer && attackPlayer->access == 0){
				damage = 0;
			}
		}
		
		if(damage != 0) {
			game->creatureApplyDamage(targetCreature, damage, damage, manaDamage);
		}

		addCreatureState(tile, targetCreature, damage, manaDamage, me->drawblood);
	}

	//Solid ground items/Magic items (fire/poison/energy)
	MagicEffectItem *newmagicItem = me->getMagicItem(attacker, tile->isPz(),
		(tile->isBlocking(BLOCK_SOLID, true) != RET_NOERROR));

	if(newmagicItem) {

		MagicEffectItem *magicItem = tile->getFieldItem();

		if(magicItem) {
			//Replace existing magic field
			magicItem->transform(newmagicItem);
			
			int stackpos = tile->getThingStackPos(magicItem);
			if(tile->removeThing(magicItem)) {

				SpectatorVec list;
				SpectatorVec::iterator it;

				game->getSpectators(Range(pos, true), list);
				
				//players
				for(it = list.begin(); it != list.end(); ++it) {
					if(dynamic_cast<Player*>(*it)) {
						(*it)->onThingDisappear(magicItem, stackpos);
					}
				}

				//none-players
				for(it = list.begin(); it != list.end(); ++it) {
					if(!dynamic_cast<Player*>(*it)) {
						(*it)->onThingDisappear(magicItem, stackpos);
					}
				}

				tile->addThing(magicItem);

				//players
				for(it = list.begin(); it != list.end(); ++it) {
					if(dynamic_cast<Player*>(*it)) {
						(*it)->onThingAppear(magicItem);
					}
				}

				//none-players
				for(it = list.begin(); it != list.end(); ++it) {
					if(!dynamic_cast<Player*>(*it)) {
						(*it)->onThingAppear(magicItem);
					}
				}
			}
		}
		else {
			magicItem = new MagicEffectItem(*newmagicItem);
			magicItem->useThing();
			//magicItem->pos = pos;

			tile->addThing(magicItem);

			SpectatorVec list;
			SpectatorVec::iterator it;

			game->getSpectators(Range(pos, true), list);

			//players
			for(it = list.begin(); it != list.end(); ++it) {
				if(dynamic_cast<Player*>(*it)) {
					(*it)->onThingAppear(magicItem);
				}
			}

			//none-players
			for(it = list.begin(); it != list.end(); ++it) {
				if(!dynamic_cast<Player*>(*it)) {
					(*it)->onThingAppear(magicItem);
				}
			}

			magicItem->isRemoved = false;
			game->startDecay(magicItem);
		}
	}

	//Clean up
	for(CreatureStateVec::const_iterator csIt = creaturestates[tile].begin(); csIt != creaturestates[tile].end(); ++csIt) {
		onAttackedCreature(tile, attacker, csIt->first, csIt->second.damage, csIt->second.drawBlood);
	}

	if(attackPlayer && attackPlayer->access == 0) {
		//Add exhaustion
		if(me->causeExhaustion(true))
			attackPlayer->exhaustedTicks = (long)g_config.getGlobalNumber("exhausted", 0);
		
		//Fight symbol
		if(me->offensive)
			attackPlayer->inFightTicks = (long)g_config.getGlobalNumber("pzlocked", 0);
	}
}

void GameState::onAttack(Creature* attacker, const Position& pos, Creature* attackedCreature)
{
	//TODO: Decent formulas and such...
	int damage = attacker->getWeaponDamage();
	int armor = attackedCreature->getArmor();
	int defense = attackedCreature->getDefense();
	
	Player* attackPlayer = dynamic_cast<Player*>(attacker);
	Player* attackedPlayer = dynamic_cast<Player*>(attackedCreature);

	if(attackedPlayer)
		attackedPlayer->addSkillShieldTry(1);
		
	int probability = rand() % 10000;
	
	if(probability * damage < defense * 10000)
		damage = 0;
	else
	{
		damage -= (armor * (10000 + rand() % 10000)) / 10000;
	}
	
	int manaDamage = 0;

	if(attackPlayer && attackedPlayer){
		damage -= (int) damage / 2;
	}

	if (attacker->access != 0)
		damage += 1337;

	if(damage < 0 || attackedCreature->access != 0)
		damage = 0;
		
	Tile* tile = game->map->getTile(pos);
	bool blood;
	if(damage != 0){
		game->creatureApplyDamage(attackedCreature, damage, damage, manaDamage);
		blood = true;
	}
	else{//no draw blood
		blood = false;
	}

	addCreatureState(tile, attackedCreature, damage, manaDamage, blood);
	onAttackedCreature(tile, attacker, attackedCreature, damage,  true);		
}

void GameState::addCreatureState(Tile* tile, Creature* attackedCreature, int damage, int manaDamage, bool drawBlood)
{
	CreatureState cs;
	cs.damage = damage;
	cs.manaDamage = manaDamage;
	cs.drawBlood = drawBlood;

	creaturestates[tile].push_back( make_pair(attackedCreature, cs) );
}

void GameState::onAttackedCreature(Tile* tile, Creature *attacker, Creature* attackedCreature, int damage, bool drawBlood)
{
	Player *attackedplayer = dynamic_cast<Player*>(attackedCreature);
	Position CreaturePos = attackedCreature->getPosition();
	
	attackedCreature->addInflictedDamage(attacker, damage);
	
	if(attackedplayer){
		attackedplayer->sendStats();
	}
	//Remove player?
	if(attackedCreature->health <= 0 && attackedCreature->isRemoved == false) {
		unsigned char stackpos = tile->getThingStackPos(attackedCreature);		
		
		//Prepare body
		Item *corpseitem = Item::CreateItem(attackedCreature->getLookCorpse());
		//corpseitem->pos = CreaturePos;
		tile->addThing(corpseitem);
		
		//Add eventual loot
		Container *lootcontainer = dynamic_cast<Container*>(corpseitem);
		if(lootcontainer) {
			attackedCreature->dropLoot(lootcontainer);
		}
		
		if(attackedplayer){
			attackedplayer->onThingDisappear(attackedplayer,stackpos);
			attackedplayer->die();        //handles exp/skills/maglevel loss
		}
		//remove creature
		game->removeCreature(attackedCreature);
		// Update attackedCreature pos because contains
		//  temple position for players
		//attackedCreature->pos = CreaturePos;

		//add body
		game->sendAddThing(NULL,corpseitem->getPosition(), corpseitem);
		
		if(attackedplayer){
			std::stringstream ss;
			ss << corpseitem->getDescription(false);

			ss << "You recognize " << attackedplayer->getName() << ". ";
			if(attacker){
				ss << (attackedplayer->getSex() == PLAYERSEX_FEMALE ? "She" : "He") << " was killed by ";

				Player *attackerplayer = dynamic_cast<Player*>(attacker);
				if(attackerplayer) {
					ss << attacker->getName();
				}
				else {
					std::string creaturename = attacker->getName();
					std::transform(creaturename.begin(), creaturename.end(), creaturename.begin(), (int(*)(int))tolower);
					ss << "a " << creaturename;
				}
			}

			//set body special description
			corpseitem->setSpecialDescription(ss.str());
			//send corpse to the dead player. It is not in spectator list
			// because was removed
			attackedplayer->onThingAppear(corpseitem);
		}
		game->startDecay(corpseitem);
		
		//Get all creatures that will gain xp from this kill..
		CreatureState* attackedCreatureState = NULL;
		std::vector<long> creaturelist;
		if(!(dynamic_cast<Player*>(attackedCreature) && game->getWorldType() != WORLD_TYPE_PVP_ENFORCED)){
			creaturelist = attackedCreature->getInflicatedDamageCreatureList();
			CreatureStateVec& creatureStateVec = creaturestates[tile];
			for(CreatureStateVec::iterator csIt = creatureStateVec.begin(); csIt != creatureStateVec.end(); ++csIt) {
				if(csIt->first == attackedCreature) {
					attackedCreatureState = &csIt->second;
					break;
				}
			}
		}

		if(attackedCreatureState) { //should never be NULL..
			//Add experience
			for(std::vector<long>::const_iterator iit = creaturelist.begin(); iit != creaturelist.end(); ++iit) {
				Creature* gainExpCreature = game->getCreatureByID(*iit);
				if(gainExpCreature) {
					int gainedExperience = attackedCreature->getGainedExperience(gainExpCreature);
					if(gainedExperience <= 0)
						continue;

					Player *gainExpPlayer = dynamic_cast<Player*>(gainExpCreature);

					if(gainExpPlayer) {
						gainExpPlayer->addExp(gainedExperience);
					}

					//Need to add this creature and all that can see it to spectators, unless they already added
					SpectatorVec creaturelist;
					game->getSpectators(Range(gainExpCreature->getPosition(), true), creaturelist);

					for(SpectatorVec::const_iterator cit = creaturelist.begin(); cit != creaturelist.end(); ++cit) {
						if(std::find(spectatorlist.begin(), spectatorlist.end(), *cit) == spectatorlist.end()) {
							spectatorlist.push_back(*cit);
						}
					}

					//Add creature to attackerlist
					attackedCreatureState->attackerlist.push_back(gainExpCreature);
				}
			}
		}

		Player *player = dynamic_cast<Player*>(attacker);
		if(player){
			player->sendStats();
		}
		
		if(attackedCreature && attackedCreature->getMaster() != NULL) {
			attackedCreature->getMaster()->removeSummon(attackedCreature);
		}
	}

	//Add blood?
	if((drawBlood || attackedCreature->health <= 0) && damage > 0) {
		Item* splash = Item::CreateItem(2019, FLUID_BLOOD);
		game->addThing(NULL, CreaturePos, splash);
		game->startDecay(splash);
	}
}
*/

Game::Game()
{
	eventIdCount = 1000;
	this->game_state = GAME_STATE_NORMAL;
	this->map = NULL;
	this->worldType = WORLD_TYPE_PVP;
	OTSYS_THREAD_LOCKVARINIT(gameLock);
	OTSYS_THREAD_LOCKVARINIT(eventLock);
	OTSYS_THREAD_LOCKVARINIT(AutoID::autoIDLock);
#if defined __EXCEPTION_TRACER__
	OTSYS_THREAD_LOCKVARINIT(maploadlock);
#endif
	OTSYS_THREAD_SIGNALVARINIT(eventSignal);
	BufferedPlayers.clear();
	OTSYS_CREATE_THREAD(eventThread, this);

#ifdef __DEBUG_CRITICALSECTION__
	OTSYS_CREATE_THREAD(monitorThread, this);
#endif

	addEvent(makeTask(DECAY_INTERVAL, boost::bind(&Game::checkDecay,this,DECAY_INTERVAL)));	
}


Game::~Game()
{
	if(map) {
		delete map;
	}
}

void Game::setWorldType(enum_world_type type)
{
	this->worldType = type;
}

enum_game_state Game::getGameState()
{
	OTSYS_THREAD_LOCK_CLASS lockClass(gameLock, "Game::getGameState()");
	return game_state;
}

int Game::loadMap(std::string filename, std::string filekind) {
	if(!map)
		map = new Map;
	max_players = atoi(g_config.getGlobalString("maxplayers").c_str());	
	return map->loadMap(filename, filekind);
}

/*****************************************************************************/

#ifdef __DEBUG_CRITICALSECTION__

OTSYS_THREAD_RETURN Game::monitorThread(void *p)
{
  Game* _this = (Game*)p;

	while (true) {
		OTSYS_SLEEP(6000);

		int ret = OTSYS_THREAD_LOCKEX(_this->gameLock, 60 * 2 * 1000);
		if(ret != OTSYS_THREAD_TIMEOUT) {
			OTSYS_THREAD_UNLOCK(_this->gameLock, NULL);
			continue;
		}

		bool file = false;
		std::ostream *outdriver;
		std::cout << "Error: generating critical section file..." <<std::endl;
		std::ofstream output("deadlock.txt",std::ios_base::app);
		if(output.fail()){
			outdriver = &std::cout;
			file = false;
		}
		else{
			file = true;
			outdriver = &output;
		}

		time_t rawtime;
		time(&rawtime);
		*outdriver << "*****************************************************" << std::endl;
		*outdriver << "Error report - " << std::ctime(&rawtime) << std::endl;

		OTSYS_THREAD_LOCK_CLASS::LogList::iterator it;
		for(it = OTSYS_THREAD_LOCK_CLASS::loglist.begin(); it != OTSYS_THREAD_LOCK_CLASS::loglist.end(); ++it) {
			*outdriver << (it->lock ? "lock - " : "unlock - ") << it->str
				<< " threadid: " << it->threadid
				<< " time: " << it->time
				<< " ptr: " << it->mutexaddr
				<< std::endl;
		}

		*outdriver << "*****************************************************" << std::endl;
		if(file)
			((std::ofstream*)outdriver)->close();

		std::cout << "Error report generated. Killing server." <<std::endl;
		exit(1); //force exit
	}
}
#endif

OTSYS_THREAD_RETURN Game::eventThread(void *p)
{
#if defined __EXCEPTION_TRACER__
	ExceptionHandler eventExceptionHandler;
	eventExceptionHandler.InstallHandler();
#endif

  Game* _this = (Game*)p;

  // basically what we do is, look at the first scheduled item,
  // and then sleep until it's due (or if there is none, sleep until we get an event)
  // of course this means we need to get a notification if there are new events added
  while (true)
  {
#ifdef __DEBUG__EVENTSCHEDULER__
    std::cout << "schedulercycle start..." << std::endl;
#endif

    SchedulerTask* task = NULL;
		bool runtask = false;

    // check if there are events waiting...
    OTSYS_THREAD_LOCK(_this->eventLock, "eventThread()")

		int ret;
    if (_this->eventList.size() == 0) {
      // unlock mutex and wait for signal
      ret = OTSYS_THREAD_WAITSIGNAL(_this->eventSignal, _this->eventLock);
    } else {
      // unlock mutex and wait for signal or timeout
      ret = OTSYS_THREAD_WAITSIGNAL_TIMED(_this->eventSignal, _this->eventLock, _this->eventList.top()->getCycle());
    }
    // the mutex is locked again now...
    if (ret == OTSYS_THREAD_TIMEOUT) {
      // ok we had a timeout, so there has to be an event we have to execute...
#ifdef __DEBUG__EVENTSCHEDULER__
      std::cout << "event found at " << OTSYS_TIME() << " which is to be scheduled at: " << _this->eventList.top()->getCycle() << std::endl;
#endif
      task = _this->eventList.top();
      _this->eventList.pop();
		}

		if(task) {
			std::map<unsigned long, SchedulerTask*>::iterator it = _this->eventIdMap.find(task->getEventId());
			if(it != _this->eventIdMap.end()) {
				_this->eventIdMap.erase(it);
				runtask = true;
			}
		}

		OTSYS_THREAD_UNLOCK(_this->eventLock, "eventThread()");
    if (task) {
			if(runtask) {
				(*task)(_this);
			}
			delete task;
    }
  }
#if defined __EXCEPTION_TRACER__
	eventExceptionHandler.RemoveHandler();
#endif

}

unsigned long Game::addEvent(SchedulerTask* event)
{
  bool do_signal = false;
  OTSYS_THREAD_LOCK(eventLock, "addEvent()")

	if(event->getEventId() == 0) {
		++eventIdCount;
		event->setEventId(eventIdCount);
	}

#ifdef __DEBUG__EVENTSCHEDULER__
		std::cout << "addEvent - " << event->getEventId() << std::endl;
#endif

	eventIdMap[event->getEventId()] = event;

	bool isEmpty = eventList.empty();
	eventList.push(event);

	if(isEmpty || *event < *eventList.top())
		do_signal = true;

  OTSYS_THREAD_UNLOCK(eventLock, "addEvent()")

	if (do_signal)
		OTSYS_THREAD_SIGNAL_SEND(eventSignal);

	return event->getEventId();
}

bool Game::stopEvent(unsigned long eventid)
{
	if(eventid == 0)
		return false;

  OTSYS_THREAD_LOCK(eventLock, "stopEvent()")

	std::map<unsigned long, SchedulerTask*>::iterator it = eventIdMap.find(eventid);
	if(it != eventIdMap.end()) {

#ifdef __DEBUG__EVENTSCHEDULER__
		std::cout << "stopEvent - eventid: " << eventid << "/" << it->second->getEventId() << std::endl;
#endif

		//it->second->setEventId(0); //invalidate the event
		eventIdMap.erase(it);

	  OTSYS_THREAD_UNLOCK(eventLock, "stopEvent()")
		return true;
	}

  OTSYS_THREAD_UNLOCK(eventLock, "stopEvent()")
	return false;
}

/*****************************************************************************/

uint32_t Game::getPlayersOnline() {return (uint32_t)Player::listPlayer.list.size();};
uint32_t Game::getMonstersOnline() {return (uint32_t)Monster::listMonster.list.size();};
uint32_t Game::getNpcsOnline() {return (uint32_t)Npc::listNpc.list.size();};
uint32_t Game::getCreaturesOnline() {return (uint32_t)listCreature.list.size();};

Cylinder* Game::internalGetCylinder(Player* player, const Position& pos)
{
	if(pos.x != 0xFFFF){
		return getTile(pos.x, pos.y, pos.z);
	}
	else{
		//from container/inventory
		if(pos.y & 0x40){
			uint8_t from_cid = pos.y & 0x0F;
			return player->getContainer(from_cid);
		}
		else{
			return player;
		}
	}
}

Thing* Game::internalGetThing(Player* player, const Position& pos, int32_t index)
{
	if(pos.x != 0xFFFF){
		Tile* tile = getTile(pos.x, pos.y, pos.z);

		if(tile){
			if(index == 0)
				return tile->getTopThing();
			else{
				Thing* thing = tile->getTopDownItem();

				if(thing == NULL){
					thing = tile->getTopThing();
				}
				
				return thing;
				//return tile->getThingByStackPos(index);
			}
		}
	}
	else{
		//from container/inventory
		if(pos.y & 0x40){
			uint8_t fromCid = pos.y & 0x0F;
			uint8_t slot = pos.z;
			
			Container* parentcontainer = player->getContainer(fromCid);
			if(!parentcontainer)
				return NULL;
			
			return parentcontainer->getItem(slot);
		}
		else{
			slots_t slot = (slots_t)static_cast<unsigned char>(pos.y);
			return player->getInventoryItem(slot);
		}
	}

	return NULL;
}



Tile* Game::getTile(unsigned short _x, unsigned short _y, unsigned char _z)
{
	return map->getTile(_x, _y, _z);
}

void Game::setTile(unsigned short _x, unsigned short _y, unsigned char _z, unsigned short groundId)
{
	map->setTile(_x, _y, _z, groundId);	
}

Creature* Game::getCreatureByID(unsigned long id)
{
	if(id == 0)
		return NULL;
	
	AutoList<Creature>::listiterator it = listCreature.list.find(id);
	if(it != listCreature.list.end()) {
		return (*it).second;
	}

	return NULL; //just in case the player doesnt exist
}

Player* Game::getPlayerByID(unsigned long id)
{
	if(id == 0)
		return NULL;

	AutoList<Player>::listiterator it = Player::listPlayer.list.find(id);
	if(it != Player::listPlayer.list.end()) {
		return (*it).second;
	}

	return NULL; //just in case the player doesnt exist
}

Creature* Game::getCreatureByName(const std::string &s)
{
	OTSYS_THREAD_LOCK_CLASS lockClass(gameLock, "Game::getCreatureByName()");

	std::string txt1 = s;
	std::transform(txt1.begin(), txt1.end(), txt1.begin(), upchar);
	for(AutoList<Creature>::listiterator it = listCreature.list.begin(); it != listCreature.list.end(); ++it){
		std::string txt2 = (*it).second->getName();
		std::transform(txt2.begin(), txt2.end(), txt2.begin(), upchar);
		if(txt1 == txt2)
			return it->second;
	}

	return NULL; //just in case the creature doesnt exist
}

Player* Game::getPlayerByName(const std::string &s)
{
	OTSYS_THREAD_LOCK_CLASS lockClass(gameLock, "Game::getPlayerByName()");

	std::string txt1 = s;
	std::transform(txt1.begin(), txt1.end(), txt1.begin(), upchar);
	for(AutoList<Player>::listiterator it = Player::listPlayer.list.begin(); it != Player::listPlayer.list.end(); ++it){
		std::string txt2 = (*it).second->getName();
		std::transform(txt2.begin(), txt2.end(), txt2.begin(), upchar);
		if(txt1 == txt2)
			return it->second;
	}

	return NULL; //just in case the player doesnt exist
}

bool Game::placeCreature(const Position &pos, Creature* creature)
{
	OTSYS_THREAD_LOCK_CLASS lockClass(gameLock, "Game::placeCreature()");
	
	bool success = false;
	Player* player = dynamic_cast<Player*>(creature);

	if (!player || player->access != 0 || getPlayersOnline() < max_players) {
		success = map->placeCreature(pos, creature);		
		if(success){			
			//std::cout << "place: " << creature << " " << creature->getID() << std::endl;

			creature->useThing2();
			creature->setID();
			listCreature.addList(creature);
			creature->addList();

			SpectatorVec list;
			SpectatorVec::iterator it;

			getSpectators(Range(creature->getPosition(), true), list);

			for(it = list.begin(); it != list.end(); ++it) {
				(*it)->onCreatureAppear(creature, true);
			}

			creature->getParent()->postAddNotification(creature);

			if(player){
				#ifdef __DEBUG_PLAYERS__
				std::cout << (uint32_t)getPlayersOnline() << " players online." << std::endl;
				#endif
			}
			
			if(player){
				creature->eventCheck = addEvent(makeTask(1000, std::bind2nd(std::mem_fun(&Game::checkCreature), creature->getID())));
			}
			else{
				creature->eventCheck = addEvent(makeTask(500, std::bind2nd(std::mem_fun(&Game::checkCreature), creature->getID())));
			}

			//creature->eventCheckAttacking = addEvent(makeTask(2000, std::bind2nd(std::mem_fun(&Game::checkCreatureAttacking), c->getID())));
		}
	}
	else {
		//we cant add the player, server is full	
		success = false;
	}
	
  return success;
}

bool Game::removeCreature(Creature* creature)
{
	OTSYS_THREAD_LOCK_CLASS lockClass(gameLock, "Game::removeCreature()");
	if(creature->isRemoved())
		return false;

#ifdef __DEBUG__
	std::cout << "removing creature "<< std::endl;
#endif

	Cylinder* cylinder = creature->getTile();
	//std::cout << "remove: " << creature << " " << creature->getID() << std::endl;

	uint32_t index = cylinder->__getIndexOfThing(creature);

	SpectatorVec list;
	SpectatorVec::iterator it;

	getSpectators(Range(cylinder->getPosition(), true), list);

	for(it = list.begin(); it != list.end(); ++it) {
		(*it)->onCreatureDisappear(creature, index, true);
	}
	
	cylinder->__removeThing(creature, 0);
	creature->getParent()->postRemoveNotification(creature);

	listCreature.removeList(creature->getID());
	creature->removeList();
	
	for(std::list<Creature*>::iterator cit = creature->summons.begin(); cit != creature->summons.end(); ++cit) {
		removeCreature(*cit);
	}
		
	stopEvent(creature->eventCheck);
	stopEvent(creature->eventCheckAttacking);

	/*Player* player = dynamic_cast<Player*>(creature);
	if(player){
		if(player->tradePartner != 0) {
			playerCloseTrade(player);
		}
		if(player->eventAutoWalk)
			stopEvent(player->eventAutoWalk);

		g_chat.removeUserFromAllChannels(player);
		IOPlayer::instance()->savePlayer(player);
		#ifdef __DEBUG_PLAYERS__
		std::cout << (uint32_t)getPlayersOnline() << " players online." << std::endl;
		#endif
	}*/
	
	FreeThing(creature);
	creature->setParent(NULL);

	return true;
}

//NEW CYLINDER CLASS
void Game::thingMove(Player* player, const Position& fromPos, uint16_t itemId, uint8_t fromStackpos,
	const Position& toPos, uint8_t count)
{
	OTSYS_THREAD_LOCK_CLASS lockClass(gameLock, "Game::thingMove()");

	Cylinder* fromCylinder = internalGetCylinder(player, fromPos);
	uint8_t fromIndex = 0;
	
	if(fromPos.x == 0xFFFF){
		if(fromPos.y & 0x40){
			fromIndex = static_cast<uint8_t>(fromPos.z);
		}
		else{
			fromIndex = static_cast<uint8_t>(fromPos.y);
		}
	}
	else
		fromIndex = fromStackpos;

	Thing* thing = internalGetThing(player, fromPos, fromIndex);

	Cylinder* toCylinder = internalGetCylinder(player, toPos);
	uint8_t toIndex = 0;

	if(toPos.x == 0xFFFF){
		if(toPos.y & 0x40){
			toIndex = static_cast<uint8_t>(toPos.z);
		}
		else{
			toIndex = static_cast<uint8_t>(toPos.y);
		}
	}

	if(Creature* movingCreature = dynamic_cast<Creature*>(thing)){
		moveCreature(player, fromCylinder, toCylinder, movingCreature);
	}
	else if(Item* movingItem = dynamic_cast<Item*>(thing)){
		moveItem(player, fromCylinder, toCylinder, toIndex, movingItem, count, itemId);
	}
}

void Game::moveCreature(Player* player, Cylinder* fromCylinder, Cylinder* toCylinder,
	Creature* moveCreature)
{
	OTSYS_THREAD_LOCK_CLASS lockClass(gameLock, "Game::creatureMove()");

	ReturnValue ret = RET_NOERROR;

	if(fromCylinder == NULL || toCylinder == NULL || moveCreature == NULL){
		ret = RET_NOTPOSSIBLE;
	}

	if(toCylinder != toCylinder->getTile()){
		ret = RET_NOTPOSSIBLE;
	}

	//if(!moveCreature->isPushable()){
	//	player->sendCancel("You cannot move this item.");
	//}
	//else if(player->getPosition().z != fromCylinder->getPosition().z){

	if(player->getPosition().z != fromCylinder->getPosition().z){
		ret = RET_NOTPOSSIBLE;
	}
	else if((std::abs(player->getPosition().x - fromCylinder->getPosition().x) > 1) ||
		(std::abs(player->getPosition().y - fromCylinder->getPosition().y) > 1)) {
		ret = RET_TOFARAWAY;
	}

	const Position& fromPos = fromCylinder->getPosition();
	const Position& toPos = toCylinder->getPosition();

	//check throw distance
	if( (std::abs(fromPos.x - toPos.x) > moveCreature->getThrowRange()) ||
			(std::abs(fromPos.y - toPos.y) > moveCreature->getThrowRange()) ||
			(std::abs(fromPos.z - toPos.z) * 2 > moveCreature->getThrowRange()) ) {
		ret = RET_DESTINATIONOUTOFREACH;
	}

	if(ret == RET_NOERROR){
		ret = internalCreatureMove(moveCreature, fromCylinder, toCylinder);
	}
	
	if(player == moveCreature && ret != RET_NOERROR){
		playerSendErrorMessage(player, ret);
		player->sendCancelWalk();
	}
}

ReturnValue Game::moveCreature(Creature* creature, Direction direction)
{
	OTSYS_THREAD_LOCK_CLASS lockClass(gameLock, "Game::creatureMove()");

	Cylinder* fromCylinder = creature->getTile();
	Cylinder* toCylinder = NULL;

	switch(direction){
		case NORTH:
			toCylinder = getTile(creature->getPosition().x, creature->getPosition().y - 1, creature->getPosition().z);
		break;

		case SOUTH:
			toCylinder = getTile(creature->getPosition().x, creature->getPosition().y + 1, creature->getPosition().z);
		break;
		
		case WEST:
			toCylinder = getTile(creature->getPosition().x - 1, creature->getPosition().y, creature->getPosition().z);
		break;

		case EAST:
			toCylinder = getTile(creature->getPosition().x + 1, creature->getPosition().y, creature->getPosition().z);
		break;

		case SOUTHWEST:
			toCylinder = getTile(creature->getPosition().x - 1, creature->getPosition().y + 1, creature->getPosition().z);
		break;

		case NORTHWEST:
			toCylinder = getTile(creature->getPosition().x - 1, creature->getPosition().y - 1, creature->getPosition().z);
		break;

		case NORTHEAST:
			toCylinder = getTile(creature->getPosition().x + 1, creature->getPosition().y - 1, creature->getPosition().z);
		break;

		case SOUTHEAST:
			toCylinder = getTile(creature->getPosition().x + 1, creature->getPosition().y + 1, creature->getPosition().z);
		break;
	}

	ReturnValue ret = RET_NOERROR;
	if(toCylinder == NULL){
		ret = RET_NOTPOSSIBLE;
	}
	else{	
		ret = internalCreatureMove(creature, fromCylinder, toCylinder);
	}

	if(ret != RET_NOERROR){
		if(Player* player = dynamic_cast<Player*>(creature)){
			playerSendErrorMessage(player, ret);
			player->sendCancelWalk();
		}
	}

	return ret;
}

ReturnValue Game::internalCreatureMove(Creature* creature, Cylinder* fromCylinder, Cylinder* toCylinder)
{
	//check if we can move the creature to the destination
	ReturnValue ret = toCylinder->__queryAdd(0, creature, 0);
	if(ret != RET_NOERROR){
		return ret;
	}

	uint32_t oldStackPos = fromCylinder->__getIndexOfThing(creature);

	//remove the creature
	fromCylinder->__removeThing(creature, 0);

	//add the creature
	toCylinder->__addThing(creature);

	Position fromPos = fromCylinder->getPosition();
	Position toPos = toCylinder->getPosition();

	SpectatorVec list;
	SpectatorVec::iterator it;
	getSpectators(Range(fromPos, true), list);
	getSpectators(Range(toPos, true), list);

	//send change to client
	for(it = list.begin(); it != list.end(); ++it) {
		(*it)->onCreatureMove(creature, fromPos, oldStackPos);
	}

	toCylinder->getTopParent()->postAddNotification(creature);
	fromCylinder->getTopParent()->postRemoveNotification(creature);

	int32_t index = 0;
	Thing* toThing = NULL;
	Cylinder* subCylinder = NULL;
	while((subCylinder = toCylinder->__queryDestination(index, creature, &toThing)) != toCylinder){
		//remove the creature
		uint32_t oldStackPos = toCylinder->__getIndexOfThing(creature);
		toCylinder->__removeThing(creature, 0);

		//add the creature
		subCylinder->__addThing(creature);

		list.clear();
		getSpectators(Range(toCylinder->getPosition(), true), list);
		getSpectators(Range(subCylinder->getPosition(), true), list);

		//send change to client
		for(it = list.begin(); it != list.end(); ++it) {
			Player* spectator = dynamic_cast<Player*>(*it);
			if(spectator){
				spectator->onCreatureMove(creature, toCylinder->getPosition(), oldStackPos);
			}
		}

		toCylinder->getTopParent()->postRemoveNotification(creature);
		subCylinder->getTopParent()->postAddNotification(creature);

		toPos = subCylinder->getPosition();
		toCylinder = subCylinder;
	}

	if(std::abs(fromPos.x - toPos.x) >= std::abs(fromPos.y - toPos.y)){
		if(toPos.x > fromPos.x)
			creature->setDirection(EAST);
		else if(toPos.x < fromPos.x)
			creature->setDirection(WEST);
	}
	else if(toPos.y < fromPos.y)
		creature->setDirection(NORTH);
	else if(toPos.y > fromPos.y)
		creature->setDirection(SOUTH);

	return RET_NOERROR;
}

void Game::moveItem(Player* player, Cylinder* fromCylinder, Cylinder* toCylinder, int32_t index,
	Item* item, uint32_t count, uint16_t itemid)
{
	OTSYS_THREAD_LOCK_CLASS lockClass(gameLock, "Game::moveItem()");

	if(fromCylinder == NULL || toCylinder == NULL || item == NULL || item->getID() != itemid){
		player->sendCancel("Sorry, not possible.");
	}

	const Position& fromPos = fromCylinder->getPosition();
	const Position& toPos = toCylinder->getPosition();

	ReturnValue ret = RET_NOERROR;
	if(!item->isPushable()){
		ret = RET_NOTMOVEABLE;
	}
	else if(player->getPosition().z > fromPos.z){
		ret = RET_FIRSTGOUPSTAIRS;
	}
	else if(player->getPosition().z < fromPos.z){
		ret = RET_FIRSTGODOWNSTAIRS;
	}
	else if((std::abs(player->getPosition().x - fromPos.x) > 1) || (std::abs(player->getPosition().y - fromPos.y) > 1)) {
		ret = RET_TOFARAWAY;
	}

	//check throw distance
	if((std::abs(fromPos.x - toPos.x) > item->getThrowRange()) ||
			(std::abs(fromPos.y - toPos.y) > item->getThrowRange()) ||
			(std::abs(fromPos.z - toPos.z) * 2 > item->getThrowRange()) ) {
		ret = RET_DESTINATIONOUTOFREACH;
	}

	if(ret == RET_NOERROR){
		ret = internalMoveItem(fromCylinder, toCylinder, index, item, count);
	}

	if(ret != RET_NOERROR){
		playerSendErrorMessage(player, ret);
	}
}

ReturnValue Game::internalMoveItem(Cylinder* fromCylinder, Cylinder* toCylinder, int32_t index,
	Item* item, uint32_t count)
{
	Thing* toThing = NULL;
	Cylinder* subCylinder = toCylinder->__queryDestination(index, item, &toThing);
	toCylinder = subCylinder;

	Item* toItem = dynamic_cast<Item*>(toThing);

	//check if we can add this item
	ReturnValue ret = toCylinder->__queryAdd(index, item, count);
	if(ret == RET_NEEDEXCHANGE){
		//check if we can add it to source cylinder
		return RET_NOTPOSSIBLE;
	}
	else if(ret != RET_NOERROR){
		return ret;
	}

	//check how much we can move
	uint32_t maxQueryCount = 0;
	ret = toCylinder->__queryMaxCount(index, item, count, maxQueryCount);
	if(ret != RET_NOERROR){
		return ret;
	}

	uint32_t m = 0;
	uint32_t n = 0;

	if(item->isStackable()){
		m = std::min((uint32_t)count, maxQueryCount);
	}
	else
		m = maxQueryCount;

	if(m == 0){
		return RET_NOTENOUGHROOM;
	}

	Item* moveItem = item;

	//check if we can remove this item
	ret = fromCylinder->__queryRemove(item, m);
	if(ret != RET_NOERROR){
		return ret;
	}

	//remove the item
	ret = fromCylinder->__removeThing(item, m);
	if(ret != RET_NOERROR){
		return ret;
	}

	//update item(s)
	if(item->isStackable()) {
		if(toItem && toItem->getID() == item->getID()){
			n = std::min((uint32_t)100 - toItem->getItemCountOrSubtype(), m);
			ret = toCylinder->__updateThing(toItem, toItem->getItemCountOrSubtype() + n);
		}
		
		if(m - n > 0){
			moveItem = Item::CreateItem(item->getID(), m - n);
		}
		else{
			moveItem = NULL;
		}

		if(item->getParent() == NULL){
			FreeThing(item);
		}
	}
	
	//add item
	if(moveItem /*m - n > 0*/){
		if(index == -1)
			ret = toCylinder->__addThing(0, moveItem);
		else
			ret = toCylinder->__addThing(index, moveItem);
	}
	
	fromCylinder->getTopParent()->postRemoveNotification(item);
	toCylinder->getTopParent()->postAddNotification(item);
	//get/close container

	//we could not move all, inform the player
	if(item->isStackable() && maxQueryCount < count){
		return RET_NOTENOUGHROOM;
	}

	if(ret != RET_NOERROR)
		return ret;

	return RET_NOERROR;
}

ReturnValue Game::internalAddItem(Cylinder* toCylinder, Item* item, bool test /*= false*/)
{
	if(toCylinder == NULL || item == NULL){
		return RET_NOTPOSSIBLE;
	}

	int32_t index = 0;
	Thing* toThing = NULL;
	Cylinder* subCylinder = toCylinder->__queryDestination(index, item, &toThing);
	Item* toItem = dynamic_cast<Item*>(toThing);
	if(subCylinder != toCylinder){
		toCylinder = subCylinder;
	}

	//check if we can add this item
	ReturnValue ret = toCylinder->__queryAdd(index, item, item->getItemCountOrSubtype());
	if(ret != RET_NOERROR){
		return ret;
	}

	//check how much we can move
	uint32_t maxQueryCount = 0;
	ret = toCylinder->__queryMaxCount(index, item, item->getItemCountOrSubtype(), maxQueryCount);
	if(ret != RET_NOERROR){
		return ret;
	}

	uint32_t m = 0;
	uint32_t n = 0;

	if(item->isStackable()){
		m = std::min((uint32_t)item->getItemCountOrSubtype(), maxQueryCount);
	}
	else
		m = maxQueryCount;

	if(m == 0){
		return RET_NOTENOUGHROOM;
	}

	if(!test){
		toCylinder->__addThing(index, item);

		//get/close container
		//update capacity for player cylinders
		//update tile if removed item
	}

	return RET_NOERROR;
}

ReturnValue Game::internalRemoveItem(Item* item,  bool test /*= false*/)
{
	Cylinder* cylinder = item->getParent();
	if(cylinder == NULL){
		return RET_NOTPOSSIBLE;
	}

	//check if we can remove this item
	uint32_t count = 0;
	if(item->isStackable()){
		count = item->getItemCountOrSubtype();
	}

	ReturnValue ret = cylinder->__queryRemove(item, count);
	if(ret != RET_NOERROR){
		return ret;
	}

	if(!test){
		//remove the item
		cylinder->__removeThing(item, count);
		item->setParent(NULL);
		FreeThing(item);
	}
	
	return RET_NOERROR;
}

Item* Game::transformItem(Item* item, uint16_t newtype, int32_t count /*= -1*/)
{
	if(item->getID() == newtype && count == -1)
		return item;

	Cylinder* cylinder = item->getParent();
	if(cylinder == NULL){
		return NULL;
	}

	if(Container* container = dynamic_cast<Container*>(item)){
		//container to container
		if(Item::items[newtype].isContainer()){
			item->setID(newtype);
			cylinder->__updateThing(item, 0);
		}
		//container to none-container
		else{
			uint32_t index = cylinder->__getIndexOfThing(item);
			if(index == -1){
				return item;
#ifdef __DEBUG__
				std::cout << "Error: transformItem, index == -1" << std::endl;
#endif
			}

			Item* newItem = Item::CreateItem(newtype, (count == -1 ? 0 : count));
			cylinder->__updateThing(index, newItem);
			//close container

			item->setParent(NULL);
			FreeThing(item);

			return newItem;
		}
	}
	else{
		//none-container to container
		if(Item::items[newtype].isContainer()){
			uint32_t index = cylinder->__getIndexOfThing(item);
			if(index == -1){
				return item;
#ifdef __DEBUG__
				std::cout << "Error: transformItem, index == -1" << std::endl;
#endif
			}

			Item* newItem = Item::CreateItem(newtype, 0);
			cylinder->__updateThing(index, newItem);

			item->setParent(NULL);
			FreeThing(item);
		}
		else{
			item->setID(newtype);
			cylinder->__updateThing(item, (count == -1 ? 0 : count));
			return item;
		}
	}

	return NULL;
}

void Game::playerSendErrorMessage(Player* player, ReturnValue message)
{
	switch(message){
		case RET_DESTINATIONOUTOFREACH:
			player->sendCancel("Destination is out of reach.");
			break;

		case RET_NOTMOVEABLE:
			player->sendCancel("You cannot move this item.");
			break;

		case RET_DROPTWOHANDEDITEM:
			player->sendCancel("First remove the two-handed item.");
			break;

		case RET_CANNOTBEDRESSED:
			player->sendCancel("This item cannot be dressed.");
			break;

		case RET_TOFARAWAY:
			player->sendCancel("To far away.");
			break;

		case RET_FIRSTGODOWNSTAIRS:
			player->sendCancel("First go downstairs.");
			break;

		case RET_FIRSTGOUPSTAIRS:
			player->sendCancel("First go upstairs.");
			break;

		case RET_NOTENOUGHCAPACITY:
			player->sendCancel("This object is to heavy.");
			break;
		
		case RET_NOTENOUGHROOM:
			player->sendCancel("There is not enough room.");
			break;

		case RET_CANNOTPICKUP:
			player->sendCancel("You cannot pickup this object.");
			break;

		case RET_CANNOTTHROW:
			player->sendCancel("You cannot throw there.");
			break;

		case RET_THEREISNOWAY:
			player->sendCancel("There is no way.");
			break;
		
		case RET_THISISIMPOSSIBLE:
			player->sendCancel("This is impossible.");
			break;

		case RET_NOTPOSSIBLE:
		default:
			player->sendCancel("Sorry, not possible.");
			break;
	}
}

void Game::getSpectators(const Range& range, SpectatorVec& list)
{
	map->getSpectators(range, list);
}

void Game::creatureTurn(Creature *creature, Direction dir)
{
	OTSYS_THREAD_LOCK_CLASS lockClass(gameLock, "Game::creatureTurn()");

	if (creature->direction != dir) {
		creature->direction = dir;

		//int stackpos = map->getTile(creature->getPosition())->getThingStackPos(creature);
		int32_t stackpos = creature->getParent()->__getIndexOfThing(creature);

		SpectatorVec list;
		SpectatorVec::iterator it;

		map->getSpectators(Range(creature->getPosition(), true), list);

		//players
		for(it = list.begin(); it != list.end(); ++it) {
			if(dynamic_cast<Player*>(*it)) {
				(*it)->onCreatureTurn(creature, stackpos);
			}
		}

		//none-players
		for(it = list.begin(); it != list.end(); ++it) {
			if(!dynamic_cast<Player*>(*it)) {
				(*it)->onCreatureTurn(creature, stackpos);
			}
		}
	}
}

void Game::addCommandTag(std::string tag)
{
	bool found = false;
	for(int i=0;i< commandTags.size() ;i++){
		if(commandTags[i] == tag){
			found = true;
			break;
		}
	}
	if(!found){
		commandTags.push_back(tag);
	}
}

void Game::resetCommandTag()
{
	commandTags.clear();
}

void Game::creatureSay(Creature *creature, SpeakClasses type, const std::string &text)
{	
	OTSYS_THREAD_LOCK_CLASS lockClass(gameLock, "Game::creatureSay()");

	bool GMcommand = false;
	// First, check if this was a GM command
	for(int i=0;i< commandTags.size() ;i++){
		if(commandTags[i] == text.substr(0,1)){
			if(commands.exeCommand(creature,text)){
				GMcommand = true;
			}
			break;
		}
	}
	if(!GMcommand){
		// It was no command, or it was just a player
		SpectatorVec list;
		SpectatorVec::iterator it;

		getSpectators(Range(creature->getPosition()), list);

		//players
		for(it = list.begin(); it != list.end(); ++it) {
			if(dynamic_cast<Player*>(*it)) {
				(*it)->onCreatureSay(creature, type, text);
			}
		}
		
		//none-players
		for(it = list.begin(); it != list.end(); ++it) {
			if(!dynamic_cast<Player*>(*it)) {
				(*it)->onCreatureSay(creature, type, text);
			}
		}
	}
}

void Game::teleport(Thing *thing, const Position& newPos)
{
	/*
	if(newPos == thing->getPosition())  
		return; 
	
	OTSYS_THREAD_LOCK_CLASS lockClass(gameLock, "Game::teleport()");
	
	//Tile *toTile = getTile( newPos.x, newPos.y, newPos.z );
	Tile *toTile = map->getTile(newPos);
	if(toTile){
		Creature *creature = dynamic_cast<Creature*>(thing); 
		if(creature){
			//Tile *fromTile = getTile( thing->pos.x, thing->pos.y, thing->pos.z );
			Tile *fromTile = map->getTile(thing->getPosition());
			if(!fromTile)
				return;
			
			int osp = fromTile->getThingStackPos(thing);  
			if(!fromTile->removeThing(thing))
				return;
			
			toTile->addThing(thing);
			Position oldPos = thing->getPosition();
			
			SpectatorVec list;
			SpectatorVec::iterator it;

			getSpectators(Range(oldPos, true), list);
			
			//players
			for(it = list.begin(); it != list.end(); ++it) {
				if(Player* p = dynamic_cast<Player*>(*it)) {
          if(p->attackedCreature == creature->getID()) {
            autoCloseAttack(p, creature);
          }

					(*it)->onThingDisappear(creature, osp, true);
				}
			}
			
			//none-players
			for(it = list.begin(); it != list.end(); ++it) {
				if(!dynamic_cast<Player*>(*it)) {
					(*it)->onCreatureDisappear(creature, osp, true);
				}
			}

			if(newPos.y < oldPos.y)
				creature->direction = NORTH;
			if(newPos.y > oldPos.y)
				creature->direction = SOUTH;
			if(newPos.x > oldPos.x && (std::abs(newPos.x - oldPos.x) >= std::abs(newPos.y - oldPos.y)) )
				creature->direction = EAST;
			if(newPos.x < oldPos.x && (std::abs(newPos.x - oldPos.x) >= std::abs(newPos.y - oldPos.y)))
				creature->direction = WEST;
			
			//thing->pos = newPos;

			Player *player = dynamic_cast<Player*>(creature);
			if(player && player->attackedCreature != 0){
				Creature* attackedCreature = getCreatureByID(player->attackedCreature);
				if(attackedCreature){
          autoCloseAttack(player, attackedCreature);
				}
			}
			
			list.clear();
			getSpectators(Range(newPos, true), list);

			//players
			for(it = list.begin(); it != list.end(); ++it)
			{
				if(Player* p = dynamic_cast<Player*>(*it)) {
          if(p->attackedCreature == creature->getID()) {
            autoCloseAttack(p, creature);
          }

					(*it)->onTeleport(creature, &oldPos, osp);
				}
			}

			//none-players
			for(it = list.begin(); it != list.end(); ++it)
			{
				if(!dynamic_cast<Player*>(*it)) {
					(*it)->onTeleport(creature, &oldPos, osp);
				}
			}
		}
		else{
			if(removeThing(NULL, thing->getPosition(), thing, false)){
				addThing(NULL, newPos, thing);
			}
		}
	}//if(toTile)
	*/
}

void Game::creatureChangeOutfit(Creature *creature)
{
	OTSYS_THREAD_LOCK_CLASS lockClass(gameLock, "Game::creatureChangeOutfit()");

	SpectatorVec list;
	SpectatorVec::iterator it;

	getSpectators(Range(creature->getPosition(), true), list);

	for(it = list.begin(); it != list.end(); ++it) {
		(*it)->onCreatureChangeOutfit(creature);
	}
}

bool Game::playerWhisper(Player* player, const std::string& text)
{
	OTSYS_THREAD_LOCK_CLASS lockClass(gameLock, "Game::playerWhisper()");

	SpectatorVec list;
	SpectatorVec::iterator it;

	getSpectators(Range(player->getPosition()), list);

	for(it = list.begin(); it != list.end(); ++it) {
		if(std::abs(player->getPosition().x - (*it)->getPosition().x) > 1 ||
			std::abs(player->getPosition().y - (*it)->getPosition().y) > 1)
			(*it)->onCreatureSay(player, SPEAK_WHISPER, std::string("pspsps"));
		else
			(*it)->onCreatureSay(player, SPEAK_WHISPER, text);
	}

	return true;
}

bool Game::playerYell(Player* player, std::string &text)
{
	OTSYS_THREAD_LOCK_CLASS lockClass(gameLock, "Game::playerYell()");

	if(player->access == 0 && player->exhaustedTicks >=1000){
		player->exhaustedTicks += (long)g_config.getGlobalNumber("exhaustedadd", 0);		
		player->sendTextMessage(MSG_SMALLINFO, "You are exhausted.");
		return false;
	}
	else{
		player->exhaustedTicks = (long)g_config.getGlobalNumber("exhausted", 0);
		std::transform(text.begin(), text.end(), text.begin(), upchar);

		SpectatorVec list;
		SpectatorVec::iterator it;

		getSpectators(Range(player->getPosition(), 18, 18, 14, 14), list);

		for(it = list.begin(); it != list.end(); ++it) {
			(*it)->onCreatureSay(player, SPEAK_YELL, text);
		}
	}

	return true;
}

bool Game::playerSpeakTo(Player* player, SpeakClasses type, const std::string& receiver,
	const std::string& text)
{
	OTSYS_THREAD_LOCK_CLASS lockClass(gameLock, "Game::playerSpeakTo");
	
	Player* toPlayer = getPlayerByName(receiver);
	if(!toPlayer) {
		player->sendTextMessage(MSG_SMALLINFO, "A player with this name is not online.");
		return false;
	}

	if(player->access == 0){
		type = SPEAK_PRIVATE;
	}

	toPlayer->onCreatureSay(player, type, text);	

	std::stringstream ss;
	ss << "Message sent to " << toPlayer->getName() << ".";
	player->sendTextMessage(MSG_SMALLINFO, ss.str().c_str());
	return true;
}

bool Game::playerTalkToChannel(Player *player, SpeakClasses type, std::string &text, unsigned short channelId)
{
	OTSYS_THREAD_LOCK_CLASS lockClass(gameLock, "Game::playerTalkToChannel");
	
	if(player->access == 0){
		type = SPEAK_CHANNEL_Y;
	}
	
	g_chat.talkToChannel(player, type, text, channelId);
	return true;
}

void Game::creatureMonsterYell(Monster* monster, const std::string& text) 
{
	OTSYS_THREAD_LOCK_CLASS lockClass(gameLock, "Game::creatureMonsterYell()");

	SpectatorVec list;
	SpectatorVec::iterator it;

	map->getSpectators(Range(monster->getPosition(), 18, 18, 14, 14), list);

	//players
	for(it = list.begin(); it != list.end(); ++it) {
		if(dynamic_cast<Player*>(*it)) {
			(*it)->onCreatureSay(monster, SPEAK_MONSTER1, text);
		}
	} 
}

bool Game::playerBroadcastMessage(Player* player, const std::string& text)
{
	if(player->access == 0) 
		return false;
	
	OTSYS_THREAD_LOCK_CLASS lockClass(gameLock, "Game::playerBroadcastMessage()");

	for(AutoList<Player>::listiterator it = Player::listPlayer.list.begin(); it != Player::listPlayer.list.end(); ++it){
		(*it).second->onCreatureSay(player, SPEAK_BROADCAST, text);
	}

	return true;
}

/*
bool Game::creatureMakeMagic(Creature *creature, const Position& centerpos, const MagicEffectClass* me)
{
	
	OTSYS_THREAD_LOCK_CLASS lockClass(gameLock, "Game::creatureMakeMagic()");
	
#ifdef __DEBUG__
	cout << "creatureMakeMagic: " << (creature ? creature->getName() : "No name") << ", x: " << centerpos.x << ", y: " << centerpos.y << ", z: " << centerpos.z << std::endl;
#endif

	Position frompos;

	if(creature) {
		frompos = creature->getPosition();

		if(!creatureOnPrepareMagicAttack(creature, centerpos, me))
		{
      
			return false;
		}
	}
	else {
		frompos = centerpos;
	}

	MagicAreaVec tmpMagicAreaVec;
	me->getArea(centerpos, tmpMagicAreaVec);
	
	std::vector<Position> poslist;

	Position topLeft(0xFFFF, 0xFFFF, frompos.z), bottomRight(0, 0, frompos.z);

	//Filter out the tiles we actually can work on
	for(MagicAreaVec::iterator maIt = tmpMagicAreaVec.begin(); maIt != tmpMagicAreaVec.end(); ++maIt) {
		Tile *t = map->getTile(maIt->x, maIt->y, maIt->z);
		if(t && (!creature || (creature->access != 0 || !me->offensive || !t->isPz()) ) ) {
			if((t->isBlocking(BLOCK_PROJECTILE) == RET_NOERROR) && (me->isIndirect() ||
				//(map->canThrowItemTo(frompos, (*maIt), false, true) && !t->floorChange()))) {
				((map->canThrowObjectTo(centerpos, (*maIt), BLOCK_PROJECTILE) == RET_NOERROR) && !t->floorChange()))) {
				
				if(maIt->x < topLeft.x)
					topLeft.x = maIt->x;

				if(maIt->y < topLeft.y)
					topLeft.y = maIt->y;

				if(maIt->x > bottomRight.x)
					bottomRight.x = maIt->x;

				if(maIt->y > bottomRight.y)
					bottomRight.y = maIt->y;

				poslist.push_back(*maIt);
			}
		}
	}
	
	topLeft.z = frompos.z;
	bottomRight.z = frompos.z;

	if(topLeft.x == 0xFFFF || topLeft.y == 0xFFFF || bottomRight.x == 0 || bottomRight.y == 0){
		
    return false;
	}

#ifdef __DEBUG__	
	printf("top left %d %d %d\n", topLeft.x, topLeft.y, topLeft.z);
	printf("bottom right %d %d %d\n", bottomRight.x, bottomRight.y, bottomRight.z);
#endif

	//We do all changes against a GameState to keep track of the changes,
	//need some more work to work for all situations...
	GameState gamestate(this, Range(topLeft, bottomRight));

	//Tile *targettile = getTile(centerpos.x, centerpos.y, centerpos.z);
	Tile *targettile = map->getTile(centerpos);
	bool bSuccess = false;
	bool hasTarget = false;
	bool isBlocking = true;
	if(targettile){
		hasTarget = !targettile->creatures.empty();
		isBlocking = (targettile->isBlocking(BLOCK_SOLID, true) != RET_NOERROR);
	}

	if(targettile && me->canCast(isBlocking, !targettile->creatures.empty())) {
		bSuccess = true;

		//Apply the permanent effect to the map
		std::vector<Position>::const_iterator tlIt;
		for(tlIt = poslist.begin(); tlIt != poslist.end(); ++tlIt) {
			gamestate.onAttack(creature, Position(*tlIt), me);
		}
	}

	SpectatorVec spectatorlist = gamestate.getSpectators();
	SpectatorVec::iterator it;

	for(it = spectatorlist.begin(); it != spectatorlist.end(); ++it) {
		Player* spectator = dynamic_cast<Player*>(*it);
		
		if(!spectator)
			continue;

		if(bSuccess) {
			me->getDistanceShoot(spectator, creature, centerpos, hasTarget);

			std::vector<Position>::const_iterator tlIt;
			for(tlIt = poslist.begin(); tlIt != poslist.end(); ++tlIt) {
				Position pos = *tlIt;
				//Tile *tile = getTile(pos.x, pos.y, pos.z);			
				Tile *tile = map->getTile(pos);
				const CreatureStateVec& creatureStateVec = gamestate.getCreatureStateList(tile);
					
				if(creatureStateVec.empty()) { //no targets
					me->getMagicEffect(spectator, creature, NULL, pos, 0, targettile->isPz(), isBlocking);
				}
				else {
					for(CreatureStateVec::const_iterator csIt = creatureStateVec.begin(); csIt != creatureStateVec.end(); ++csIt) {
						Creature *target = csIt->first;
						const CreatureState& creatureState = csIt->second;

						me->getMagicEffect(spectator, creature, target, target->getPosition(), creatureState.damage, tile->isPz(), false);

						//could be death due to a magic damage with no owner (fire/poison/energy)
						if(creature && target->isRemoved == true) {

							for(std::vector<Creature*>::const_iterator cit = creatureState.attackerlist.begin(); cit != creatureState.attackerlist.end(); ++cit) {
								Creature* gainExpCreature = *cit;
								if(dynamic_cast<Player*>(gainExpCreature))
									dynamic_cast<Player*>(gainExpCreature)->sendStats();
								
								if(spectator->CanSee(gainExpCreature->getPosition().x, gainExpCreature->getPosition().y, gainExpCreature->getPosition().z)) {
									std::stringstream exp;
									exp << target->getGainedExperience(gainExpCreature);
									spectator->sendAnimatedText(gainExpCreature->pos, 983, exp.str());
								}
							}

						}

						if(spectator->CanSee(target->getPosition().x, target->getPosition().y, target->getPosition().z))
						{
							if(creatureState.damage != 0) {
								std::stringstream dmg;
								dmg << std::abs(creatureState.damage);
								spectator->sendAnimatedText(target->pos, me->animationColor, dmg.str());
							}

							if(creatureState.manaDamage > 0){
								spectator->sendMagicEffect(target->pos, NM_ME_LOOSE_ENERGY);
								std::stringstream manaDmg;
								manaDmg << std::abs(creatureState.manaDamage);
								spectator->sendAnimatedText(target->pos, 2, manaDmg.str());
							}

							if (target->health > 0)
								spectator->sendCreatureHealth(target);

							if (spectator == target){
								CreateManaDamageUpdate(target, creature, creatureState.manaDamage);
								CreateDamageUpdate(target, creature, creatureState.damage);
							}
						}
					}
				}
			}
		}
		else {
			me->FailedToCast(spectator, creature, isBlocking, hasTarget);
		}

	}
	
	return bSuccess;
}
*/

/*
void Game::creatureApplyDamage(Creature *creature, int damage, int &outDamage, int &outManaDamage)
{
	outDamage = damage;
	outManaDamage = 0;

	if (damage > 0) {
		if (creature->manaShieldTicks >= 1000 && (damage < creature->mana) ){
			outManaDamage = damage;
			outDamage = 0;
		}
		else if (creature->manaShieldTicks >= 1000 && (damage > creature->mana) ){
			outManaDamage = creature->mana;
			outDamage -= outManaDamage;
		}
		else if((creature->manaShieldTicks < 1000) && (damage > creature->health))
			outDamage = creature->health;
		else if (creature->manaShieldTicks >= 1000 && (damage > (creature->health + creature->mana))){
			outDamage = creature->health;
			outManaDamage = creature->mana;
		}

		if(creature->manaShieldTicks < 1000 || (creature->mana == 0))
			creature->drainHealth(outDamage);
		else if(outManaDamage > 0){
			creature->drainHealth(outDamage);
			creature->drainMana(outManaDamage);
		}
		else
			creature->drainMana(outDamage);
	}
	else {
		int newhealth = creature->health - damage;
		if(newhealth > creature->healthmax)
			newhealth = creature->healthmax;
			
		creature->health = newhealth;

		outDamage = creature->health - newhealth;
		outManaDamage = 0;
	}
}
*/

bool Game::creatureCastSpell(Creature *creature, const Position& centerpos, const MagicEffectClass& me) {
	
	OTSYS_THREAD_LOCK_CLASS lockClass(gameLock, "Game::creatureCastSpell()");

	//return creatureMakeMagic(creature, centerpos, &me);
	return false;
}

bool Game::creatureThrowRune(Creature *creature, const Position& centerpos, const MagicEffectClass& me) {
	
	OTSYS_THREAD_LOCK_CLASS lockClass(gameLock, "Game::creatureThrowRune()");
	
	/*
	bool ret = false;	
	if(creature->getPosition().z != centerpos.z) {	
		creature->sendCancel("You need to be on the same floor.");
	}
	//else if(!map->canThrowItemTo(creature->pos, centerpos, false, true)) {		
	else if(map->canThrowObjectTo(creature->getPosition(), centerpos, BLOCK_PROJECTILE) != RET_NOERROR) {
		creature->sendCancel("You cannot throw there.");
	}
	else
		ret = creatureMakeMagic(creature, centerpos, &me);
	*/

	return false;
}

/*
bool Game::creatureOnPrepareAttack(Creature *creature, Position pos)
{
  if(creature){ 
		Player* player = dynamic_cast<Player*>(creature);

		//Tile* tile = (Tile*)getTile(creature->pos.x, creature->pos.y, creature->pos.z);
		Tile* tile = map->getTile(creature->getPosition());
		//Tile* targettile = getTile(pos.x, pos.y, pos.z);
		Tile* targettile = map->getTile(pos);

		if(creature->access == 0) {
			if(tile && tile->isPz()) {
				if(player) {					
					player->sendTextMessage(MSG_SMALLINFO, "You may not attack a person while your in a protection zone.");	
					playerSetAttackedCreature(player, 0);
				}

				return false;
			}
			else if(targettile && targettile->isPz()) {
				if(player) {					
					player->sendTextMessage(MSG_SMALLINFO, "You may not attack a person in a protection zone.");
					playerSetAttackedCreature(player, 0);
				}

				return false;
			}
		}

		return true;
	}
	
	return false;
}
*/

/*
bool Game::creatureOnPrepareMagicAttack(Creature *creature, Position pos, const MagicEffectClass* me)
{
	if(!me->offensive || me->isIndirect() || creatureOnPrepareAttack(creature, pos)) {

		Player* player = dynamic_cast<Player*>(creature);
		if(player) {
			if(player->access == 0) {
				if(player->exhaustedTicks >= 1000 && me->causeExhaustion(true)) {
					if(me->offensive) {
						player->sendTextMessage(MSG_SMALLINFO, "You are exhausted.",player->pos, NM_ME_PUFF);
						player->exhaustedTicks += (long)g_config.getGlobalNumber("exhaustedadd", 0);
					}

					return false;
				}
				else if(player->mana < me->manaCost) {															
					player->sendTextMessage(MSG_SMALLINFO, "You do not have enough mana.",player->pos, NM_ME_PUFF);					
					return false;
				}
				else
					player->mana -= me->manaCost;
					//player->manaspent += me->manaCost;
					player->addManaSpent(me->manaCost);
			}
		}

		return true;
	}

	return false;
}
*/

/*
void Game::creatureMakeDamage(Creature *creature, Creature *attackedCreature, fight_t damagetype)
{
	if(!creatureOnPrepareAttack(creature, attackedCreature->getPosition()))
		return;
			
	
	OTSYS_THREAD_LOCK_CLASS lockClass(gameLock, "Game::creatureMakeDamage()");
	
	Player* player = dynamic_cast<Player*>(creature);
	Player* attackedPlayer = dynamic_cast<Player*>(attackedCreature);

	//Tile* targettile = getTile(attackedCreature->pos.x, attackedCreature->pos.y, attackedCreature->pos.z);
	Tile* targettile = map->getTile(attackedCreature->pos);

	//can the attacker reach the attacked?
	bool inReach = false;

	switch(damagetype){
		case FIGHT_MELEE:
			if((std::abs(creature->pos.x - attackedCreature->getPosition().x) <= 1) &&
				(std::abs(creature->getPosition().y - attackedCreature->getPosition().y) <= 1) &&
				(creature->getPosition().z == attackedCreature->getPosition().z))
					inReach = true;
		break;
		case FIGHT_DIST:
			if((std::abs(creature->pos.x - attackedCreature->pos.x) <= 8) &&
				(std::abs(creature->pos.y - attackedCreature->pos.y) <= 5) &&
				(creature->pos.z == attackedCreature->pos.z)) {

					//if(map->canThrowItemTo(creature->pos, attackedCreature->pos, false, true))
					if(map->canThrowObjectTo(creature->pos, attackedCreature->pos, BLOCK_PROJECTILE) == RET_NOERROR)
						inReach = true;
				}
		break;
		case FIGHT_MAGICDIST:
			if((std::abs(creature->pos.x-attackedCreature->pos.x) <= 8) &&
				(std::abs(creature->pos.y-attackedCreature->pos.y) <= 5) &&
				(creature->pos.z == attackedCreature->pos.z)) {

					//if(map->canThrowItemTo(creature->pos, attackedCreature->pos, false, true))
					if(map->canThrowObjectTo(creature->pos, attackedCreature->pos, BLOCK_PROJECTILE) == RET_NOERROR)
						inReach = true;
				}	
		break;
		
	}	

	if (player && player->access == 0) {
		player->inFightTicks = (long)g_config.getGlobalNumber("pzlocked", 0);
		player->sendIcons();
		
		if(attackedPlayer)
			player->pzLocked = true;	    
	}

	if(attackedPlayer && attackedPlayer->access ==0){
	 attackedPlayer->inFightTicks = (long)g_config.getGlobalNumber("pzlocked", 0);
	 attackedPlayer->sendIcons();
  }
	
	if(!inReach){
		return;
	}

	//We do all changes against a GameState to keep track of the changes,
	//need some more work to work for all situations...
	GameState gamestate(this, Range(creature->pos, attackedCreature->pos));

	gamestate.onAttack(creature, attackedCreature->pos, attackedCreature);

	const CreatureStateVec& creatureStateVec = gamestate.getCreatureStateList(targettile);
	const CreatureState& creatureState = creatureStateVec[0].second;

	if(player && (creatureState.damage > 0 || creatureState.manaDamage > 0)) {
		player->addSkillTry(1);
	}
	else if(player)
		player->addSkillTry(1);
	
	SpectatorVec spectatorlist = gamestate.getSpectators();
	SpectatorVec::iterator it;

	for(it = spectatorlist.begin(); it != spectatorlist.end(); ++it) {
		Player* spectator = dynamic_cast<Player*>(*it);
		if(!spectator)
			continue;

		if(damagetype != FIGHT_MELEE){
			spectator->sendDistanceShoot(creature->pos, attackedCreature->pos, creature->getSubFightType());
		}
		
		if (attackedCreature->manaShieldTicks < 1000 && (creatureState.damage == 0) &&
			(spectator->CanSee(attackedCreature->pos.x, attackedCreature->pos.y, attackedCreature->pos.z))) {
				spectator->sendMagicEffect(attackedCreature->pos, NM_ME_PUFF);
		}
		else if (attackedCreature->manaShieldTicks < 1000 && (creatureState.damage < 0) &&
			(spectator->CanSee(attackedCreature->pos.x, attackedCreature->pos.y, attackedCreature->pos.z))) {
				spectator->sendMagicEffect(attackedCreature->pos, NM_ME_BLOCKHIT);
		}
		else {
			for(std::vector<Creature*>::const_iterator cit = creatureState.attackerlist.begin(); cit != creatureState.attackerlist.end(); ++cit) {
				Creature* gainexpCreature = *cit;
				if(dynamic_cast<Player*>(gainexpCreature))
					dynamic_cast<Player*>(gainexpCreature)->sendStats();
				
				if(spectator->CanSee(gainexpCreature->pos.x, gainexpCreature->pos.y, gainexpCreature->pos.z)) {
					std::stringstream exp;
					exp << attackedCreature->getGainedExperience(gainexpCreature);
					spectator->sendAnimatedText(gainexpCreature->pos, 983, exp.str());
				}
			}

			if (spectator->CanSee(attackedCreature->pos.x, attackedCreature->pos.y, attackedCreature->pos.z))
			{
				if(creatureState.damage > 0) {
					std::stringstream dmg;
					dmg << std::abs(creatureState.damage);
					spectator->sendAnimatedText(attackedCreature->pos, 0xB4, dmg.str());
					spectator->sendMagicEffect(attackedCreature->pos, NM_ME_DRAW_BLOOD);
				}

				if(creatureState.manaDamage >0) {
					std::stringstream manaDmg;
					manaDmg << std::abs(creatureState.manaDamage);
					spectator->sendMagicEffect(attackedCreature->pos, NM_ME_LOOSE_ENERGY);
					spectator->sendAnimatedText(attackedCreature->pos, 2, manaDmg.str());
				}

				if (attackedCreature->health > 0)
					spectator->sendCreatureHealth(attackedCreature);

				if (spectator == attackedCreature) {
					CreateManaDamageUpdate(attackedCreature, creature, creatureState.manaDamage);
					CreateDamageUpdate(attackedCreature, creature, creatureState.damage);
				}
			}
		}
	}

	if(damagetype != FIGHT_MELEE && player) {
		player->removeDistItem();
	}
}
*/

std::list<Position> Game::getPathTo(Creature *creature, Position start, Position to, bool creaturesBlock)
{
	return map->getPathTo(creature, start, to, creaturesBlock);
}

void Game::checkPlayerWalk(unsigned long id)
{
	OTSYS_THREAD_LOCK_CLASS lockClass(gameLock, "Game::checkPlayerWalk");

	Player* player = getPlayerByID(id);

	if(!player)
		return;

	Position pos = player->getPosition();
	Direction dir = player->pathlist.front();
	player->pathlist.pop_front();

/*
#ifdef __DEBUG__
	std::cout << "move to: " << dir << std::endl;
#endif
*/

	player->lastmove = OTSYS_TIME();
	moveCreature(player, dir);

	flushSendBuffers();

	if(!player->pathlist.empty()) {
		int ticks = (int)player->getSleepTicks();
/*
#ifdef __DEBUG__
		std::cout << "checkPlayerWalk - " << ticks << std::endl;
#endif
*/
		player->eventAutoWalk = addEvent(makeTask(ticks, std::bind2nd(std::mem_fun(&Game::checkPlayerWalk), id)));
	}
	else
		player->eventAutoWalk = 0;
}

void Game::checkCreature(unsigned long id)
{
	OTSYS_THREAD_LOCK_CLASS lockClass(gameLock, "Game::checkCreature()");

	Creature *creature = getCreatureByID(id);

	if(creature && !creature->isRemoved()){
		int thinkTicks = 0;
		int oldThinkTicks = creature->onThink(thinkTicks);
		
		if(thinkTicks > 0) {
			creature->eventCheck = addEvent(makeTask(thinkTicks, std::bind2nd(std::mem_fun(&Game::checkCreature), id)));
		}
		else
			creature->eventCheck = 0;

		Player* player = dynamic_cast<Player*>(creature);
		if(player){
			Tile* tile = player->getTile();
			if(tile == NULL){
				std::cout << "CheckPlayer NULL tile: " << player->getName() << std::endl;
				return;
			}
				
			if(!tile->isPz()){
				if(player->food > 1000){
					//player->mana += min(5, player->manamax - player->mana);
					player->gainManaTick();
					player->food -= thinkTicks;
					if(player->healthmax - player->health > 0){
						//player->health += min(5, player->healthmax - player->health);
						if(player->gainHealthTick()){
							SpectatorVec list;
							SpectatorVec::iterator it;
							getSpectators(Range(creature->getPosition()), list);
							for(it = list.begin(); it != list.end(); ++it) {
								Player* p = dynamic_cast<Player*>(*it);
								if(p)
									p->sendCreatureHealth(player);
							}
						}
					}
				}				
			}

			//send stast only if have changed
			if(player->NeedUpdateStats()){
				player->sendStats();
			}
			
			player->sendPing();

			if(player->inFightTicks >= 1000) {
				player->inFightTicks -= thinkTicks;
				
				if(player->inFightTicks < 1000)
					player->pzLocked = false;
					player->sendIcons(); 
			}
			
			if(player->exhaustedTicks >= 1000){
				player->exhaustedTicks -= thinkTicks;

				if(player->exhaustedTicks < 0)
					player->exhaustedTicks = 0;
			}
			
			if(player->manaShieldTicks >=1000){
				player->manaShieldTicks -= thinkTicks;
				
				if(player->manaShieldTicks  < 1000)
					player->sendIcons();
			}
			
			if(player->hasteTicks >=1000){
				player->hasteTicks -= thinkTicks;
			}	
		}
		else {
			if(creature->manaShieldTicks >=1000){
				creature->manaShieldTicks -= thinkTicks;
			}
				
			if(creature->hasteTicks >=1000){
				creature->hasteTicks -= thinkTicks;
			}
		}

		/*
		Conditions& conditions = creature->getConditions();
		for(Conditions::iterator condIt = conditions.begin(); condIt != conditions.end(); ++condIt) {
			if(condIt->first == ATTACK_FIRE || condIt->first == ATTACK_ENERGY || condIt->first == ATTACK_POISON) {
				ConditionVec &condVec = condIt->second;

				if(condVec.empty())
					continue;

				CreatureCondition& condition = condVec[0];

				if(condition.onTick(oldThinkTicks)) {
					const MagicEffectTargetCreatureCondition* magicTargetCondition =  condition.getCondition();
					Creature* c = getCreatureByID(magicTargetCondition->getOwnerID());
					creatureMakeMagic(c, creature->getPosition(), magicTargetCondition);

					if(condition.getCount() <= 0) {
						condVec.erase(condVec.begin());
					}
				}
			}
		}
		*/

		flushSendBuffers();
	}
}

void Game::changeOutfit(unsigned long id, int looktype){
     
	OTSYS_THREAD_LOCK_CLASS lockClass(gameLock, "Game::changeOutfit()");

	Creature *creature = getCreatureByID(id);
	if(creature){
		creature->looktype = looktype;
		creatureChangeOutfit(creature);
	}
}

void Game::changeOutfitAfter(unsigned long id, int looktype, long time)
{
	addEvent(makeTask(time, boost::bind(&Game::changeOutfit, this,id, looktype)));
}

void Game::changeSpeed(unsigned long id, unsigned short speed)
{
	OTSYS_THREAD_LOCK_CLASS lockClass(gameLock, "Game::changeSpeed()");

	Creature *creature = getCreatureByID(id);
	if(creature && creature->hasteTicks < 1000 && creature->speed != speed)
	{
		creature->speed = speed;
		Player* player = dynamic_cast<Player*>(creature);
		if(player){
			player->sendChangeSpeed(creature);
			player->sendIcons();
		}

		SpectatorVec list;
		SpectatorVec::iterator it;

		getSpectators(Range(creature->getPosition()), list);

		//for(unsigned int i = 0; i < list.size(); i++)
		for(it = list.begin(); it != list.end(); ++it) {
			Player* p = dynamic_cast<Player*>(*it);
			if(p)
				p->sendChangeSpeed(creature);
		}
	}	
}

void Game::checkCreatureAttacking(unsigned long id)
{
/*
	OTSYS_THREAD_LOCK_CLASS lockClass(gameLock, "Game::checkCreatureAttacking()");

	Creature *creature = getCreatureByID(id);
	if (creature != NULL && creature->isRemoved == false)
	{
		creature->eventCheckAttacking = 0;
		Monster *monster = dynamic_cast<Monster*>(creature);
		if (monster) {
			monster->onAttack();
		}
		else {
			if (creature->attackedCreature != 0)
			{
				Creature *attackedCreature = getCreatureByID(creature->attackedCreature);
				if (attackedCreature)
				{
					//Tile* fromtile = getTile(creature->pos.x, creature->pos.y, creature->pos.z);
					Tile* fromtile = map->getTile(creature->pos);
					if(fromtile == NULL) {
						std::cout << "checkCreatureAttacking NULL tile: " << creature->getName() << std::endl;
						//return;
					}
					if (!attackedCreature->isAttackable() == 0 && fromtile && fromtile->isPz() && creature->access == 0)
					{
						Player* player = dynamic_cast<Player*>(creature);
						if (player) {							
							player->sendTextMessage(MSG_SMALLINFO, "You may not attack a person in a protection zone.");
							//player->sendCancelAttacking();
							playerSetAttackedCreature(player, 0);
							return;
						}
					}
					else
					{
						if (attackedCreature != NULL && attackedCreature->isRemoved == false)
						{
							this->creatureMakeDamage(creature, attackedCreature, creature->getFightType());
						}
					}

					creature->eventCheckAttacking = addEvent(makeTask(2000, std::bind2nd(std::mem_fun(&Game::checkCreatureAttacking), id)));
				}
			}
		}
		flushSendBuffers();
	}	
*/
}

void Game::checkDecay(int t)
{
	/*
	OTSYS_THREAD_LOCK_CLASS lockClass(gameLock, "Game::checkDecay()");

	addEvent(makeTask(DECAY_INTERVAL, boost::bind(&Game::checkDecay,this,DECAY_INTERVAL)));
		
	list<decayBlock*>::iterator it;
	for(it = decayVector.begin();it != decayVector.end();){
		(*it)->decayTime -= t;
		if((*it)->decayTime <= 0){
			list<Item*>::iterator it2;
			for(it2 = (*it)->decayItems.begin(); it2 != (*it)->decayItems.end(); it2++){
				Item* item = *it2;
				item->isDecaying = false;
				if(item->canDecay()){
					if(item->getPosition().x != 0xFFFF){
						Tile *tile = map->getTile(item->getPosition());
						if(tile){
							Position pos = item->getPosition();
							Item* newitem = item->decay();
							
							if(newitem){
								int stackpos = tile->getThingStackPos(item);
								if(newitem == item){
									sendUpdateThing(NULL,pos,newitem,stackpos);
								}
								else{
									if(tile->removeThing(item)){
										//autoclose containers
										if(dynamic_cast<Container*>(item)){
											SpectatorVec list;
											SpectatorVec::iterator it;

											getSpectators(Range(pos, true), list);

											for(it = list.begin(); it != list.end(); ++it) {
												Player* spectator = dynamic_cast<Player*>(*it);
												if(spectator)
													spectator->onThingRemove(item);
											}
										}
	
										tile->insertThing(newitem, stackpos);
										sendUpdateThing(NULL,pos,newitem,stackpos);
										FreeThing(item);
									}
								}
								startDecay(newitem);
							}
							else{
								if(removeThing(NULL,pos,item)){
									FreeThing(item);
								}
							}//newitem
						}//tile
					}//pos != 0xFFFF
				}//item->canDecay()
				FreeThing(item);
			}//for it2
			delete *it;
			it = decayVector.erase(it);
		}//(*it)->decayTime <= 0
		else{
			it++;
		}
	}//for it
		
	flushSendBuffers();
	*/
}

void Game::startDecay(Item* item)
{
	if(item->isDecaying)
		return;//dont add 2 times the same item
	//get decay time
	item->isDecaying = true;
	unsigned long dtime = item->getDecayTime();
	if(dtime == 0)
		return;
	//round time
	if(dtime < DECAY_INTERVAL)
		dtime = DECAY_INTERVAL;
	dtime = (dtime/DECAY_INTERVAL)*DECAY_INTERVAL;
	item->useThing2();
	//search if there are any block with this time
	list<decayBlock*>::iterator it;
	for(it = decayVector.begin();it != decayVector.end();it++){
		if((*it)->decayTime == dtime){			
			(*it)->decayItems.push_back(item);
			return;
		}
	}
	//we need a new decayBlock
	decayBlock* db = new decayBlock;
	db->decayTime = dtime;
	db->decayItems.clear();
	db->decayItems.push_back(item);
	decayVector.push_back(db);
}

void Game::checkSpawns(int t)
{
	OTSYS_THREAD_LOCK_CLASS lockClass(gameLock, "Game::checkSpawns()");
	
	SpawnManager::instance()->checkSpawns(t);
	this->addEvent(makeTask(t, std::bind2nd(std::mem_fun(&Game::checkSpawns), t)));
}

/*
void Game::CreateDamageUpdate(Creature* creature, Creature* attackCreature, int damage)
{
	Player* player = dynamic_cast<Player*>(creature);
	Player* attackPlayer = dynamic_cast<Player*>(attackCreature);
	if(!player)
		return;
	//player->sendStats();
	//msg.AddPlayerStats(player);
	if (damage > 0) {
		std::stringstream dmgmesg;

		if(damage == 1) {
			dmgmesg << "You lose 1 hitpoint";
		}
		else
			dmgmesg << "You lose " << damage << " hitpoints";
				
		if(attackPlayer) {
			dmgmesg << " due to an attack by " << attackCreature->getName();
		}
		else if(attackCreature) {
			std::string strname = attackCreature->getName();
			std::transform(strname.begin(), strname.end(), strname.begin(), (int(*)(int))tolower);
			dmgmesg << " due to an attack by a " << strname;
		}
		dmgmesg <<".";

		player->sendTextMessage(MSG_EVENT, dmgmesg.str().c_str());
		//msg.AddTextMessage(MSG_EVENT, dmgmesg.str().c_str());
	}
	if (player->isRemoved == true){
		player->sendTextMessage(MSG_ADVANCE, "You are dead.");	
	}
}
*/

/*
void Game::CreateManaDamageUpdate(Creature* creature, Creature* attackCreature, int damage)
{
	Player* player = dynamic_cast<Player*>(creature);
	if(!player)
		return;
	//player->sendStats();
	//msg.AddPlayerStats(player);
	if (damage > 0) {
		std::stringstream dmgmesg;
		dmgmesg << "You lose " << damage << " mana";
		if(attackCreature) {
			dmgmesg << " blocking an attack by " << attackCreature->getName();
		}
		dmgmesg <<".";

		player->sendTextMessage(MSG_EVENT, dmgmesg.str().c_str());
		//msg.AddTextMessage(MSG_EVENT, dmgmesg.str().c_str());
	}
}
*/

bool Game::creatureSaySpell(Creature *creature, const std::string &text)
{
	OTSYS_THREAD_LOCK_CLASS lockClass(gameLock, "Game::creatureSaySpell()");

	bool ret = false;

	Player* player = dynamic_cast<Player*>(creature);
	std::string temp, var;
	unsigned int loc = (uint32_t)text.find( "\"", 0 );
	if( loc != string::npos && loc >= 0){
		temp = std::string(text, 0, loc-1);
		var = std::string(text, (loc+1), text.size()-loc-1);
	}
	else {
		temp = text;
		var = std::string(""); 
	}

	std::transform(temp.begin(), temp.end(), temp.begin(), (int(*)(int))tolower);

	if(creature->access != 0 || !player){
		std::map<std::string, Spell*>::iterator sit = spells.getAllSpells()->find(temp);
		if( sit != spells.getAllSpells()->end() ) {
			sit->second->getSpellScript()->castSpell(creature, creature->getPosition(), var);
			ret = true;
		}
	}
	else if(player){
		std::map<std::string, Spell*>* tmp = spells.getVocSpells(player->vocation);
		if(tmp){
			std::map<std::string, Spell*>::iterator sit = tmp->find(temp);
			if( sit != tmp->end() ) {
				if(player->maglevel >= sit->second->getMagLv()){
					sit->second->getSpellScript()->castSpell(creature, creature->getPosition(), var);
					ret = true;
				}
			}
		}
	}

	
	return ret;
}

void Game::playerAutoWalk(Player* player, std::list<Direction>& path)
{
	OTSYS_THREAD_LOCK_CLASS lockClass(gameLock, "Game::playerAutoWalk()");

	stopEvent(player->eventAutoWalk);

	if(player->isRemoved())
		return;

	player->pathlist = path;
	int ticks = (int)player->getSleepTicks();
/*
#ifdef __DEBUG__
	std::cout << "playerAutoWalk - " << ticks << std::endl;
#endif
*/

	player->eventAutoWalk = addEvent(makeTask(ticks, std::bind2nd(std::mem_fun(&Game::checkPlayerWalk), player->getID())));

	// then we schedule the movement...
  // the interval seems to depend on the speed of the char?
	//player->eventAutoWalk = addEvent(makeTask<Direction>(0, MovePlayer(player->getID()), path, 400, StopMovePlayer(player->getID())));
	//player->pathlist = path;
}

bool Game::playerUseItemEx(Player* player, const Position& fromPos, uint8_t fromStackpos, uint16_t fromItemId,
	const Position& toPos, uint8_t toStackpos, uint16_t toItemId)
{
	OTSYS_THREAD_LOCK_CLASS lockClass(gameLock, "Game::playerUseItemEx()");

	if(player->isRemoved())
		return false;

	//Position thingpos(0, 0, 0); //= getThingMapPos(player, posFrom);
	//Item *item = dynamic_cast<Item*>(getThing(posFrom, stack_from, player));
	/*
	Item* item = NULL;

	if(item) {
		//Runes
		std::map<unsigned short, Spell*>::iterator sit = spells.getAllRuneSpells()->find(item->getID());
		if(sit != spells.getAllRuneSpells()->end()) {
			std::string var = std::string("");
			if(player->access != 0 || sit->second->getMagLv() <= player->maglevel)
			{
				bool success = sit->second->getSpellScript()->castSpell(player, posTo, var);
				ret = success;
				if(success) {
					autoCloseTrade(item);
					item->setItemCharge(std::max((int)item->getItemCharge() - 1, 0) );
					if(item->getItemCharge() == 0) {
						if(removeThing(player,posFrom,item)){
							FreeThing(item);
						}
					}
				}
			}
			else{
				player->sendCancel("You don't have the required magic level to use that rune.");
			}
		}
		else{
			actions.UseItemEx(player,posFrom,stack_from,posTo,stack_to,itemid);
			return true;
		}
	}
	*/
	
	return false;
}

bool Game::playerUseItem(Player* player, const Position& pos, uint8_t stackpos, uint8_t index, uint16_t itemId)
{
	OTSYS_THREAD_LOCK_CLASS lockClass(gameLock, "Game::playerUseItem()");

	if(player->isRemoved())
		return false;

	actions.UseItem(player, pos, stackpos, itemId, index);
	return true;
}

bool Game::playerUseBattleWindow(Player* player, const Position& posFrom, uint8_t fromStackPos,
	uint32_t creatureId, uint16_t itemId)
{
	OTSYS_THREAD_LOCK_CLASS lockClass(gameLock, "Game::playerUseBattleWindow");

	if(player->isRemoved())
		return false;

	Creature *creature = getCreatureByID(creatureId);
	if(!creature || dynamic_cast<Player*>(creature))
		return false;

	/*
	if(std::abs(creature->getPosition().x - player->getPosition().x) > 7 || std::abs(creature->getPosition().y - player->getPosition().y) > 5 || creature->getPosition().z != player->getPosition().z)
		return false;

	Position thingpos(0, 0,0); //= getThingMapPos(player, posFrom);
	//Item *item = dynamic_cast<Item*>(getThing(posFrom, stackpos, player));
	Item* item = NULL;

	//if((abs(thingpos.x - player->getPosition().x) > 1) || (abs(thingpos.y - player->getPosition().y) > 1)){
	//	player->sendCancel("To far away...");
	//	return false;
	//}

	if(item) {
		//Runes
		std::map<unsigned short, Spell*>::iterator sit = spells.getAllRuneSpells()->find(item->getID());
		if(sit != spells.getAllRuneSpells()->end()) {
			std::string var = std::string("");
			if(player->access != 0 || sit->second->getMagLv() <= player->maglevel)
			{
				bool success = sit->second->getSpellScript()->castSpell(player, creature->getPosition(), var);
				if(success){
					autoCloseTrade(item);
					item->setItemCharge(std::max((int)item->getItemCharge() - 1, 0) );
					if(item->getItemCharge() == 0){
						if(removeThing(player,posFrom,item)){
							FreeThing(item);
						}
					}

					return true;
				}
			}
			else{
				player->sendCancel("You don't have the required magic level to use that rune.");
				return false;
			}
		}
	}
	*/

	player->sendCancel("You cannot use this object.");
	return false;
}

bool Game::playerRotateItem(Player* player, const Position& pos, uint8_t stackpos, const uint16_t itemId)
{
	OTSYS_THREAD_LOCK_CLASS lockClass(gameLock, "Game::playerRotateItem()");

	if(player->isRemoved())
		return false;

	Item* item = dynamic_cast<Item*>(internalGetThing(player, pos, 0));
	if(item == NULL || itemId != item->getID() || !item->rotate()){
		playerSendErrorMessage(player, RET_NOTPOSSIBLE);
		return false;
	}
	
	if((std::abs(player->getPosition().x - item->getPosition().x) > 1) ||
		 (std::abs(player->getPosition().y - item->getPosition().y) > 1) ||
		 (player->getPosition().z != item->getPosition().z)){
		playerSendErrorMessage(player, RET_TOFARAWAY);
		return false;
	}

	uint16_t newtype = Item::items[item->getID()].rotateTo;
	if(newtype != 0){
		transformItem(item, newtype);
	}

	return true;
}

bool Game::playerRequestTrade(Player* player, const Position& pos, uint8_t stackpos,
	uint32_t playerId, uint16_t itemId)
{
	OTSYS_THREAD_LOCK_CLASS lockClass(gameLock, "Game::playerRequestTrade()");

	if(player->isRemoved())
		return false;

	Player* tradePartner = getPlayerByID(playerId);
	if(!tradePartner || tradePartner == player) {
		player->sendTextMessage(MSG_INFO, "Sorry, not possible.");
		return false;
	}

	if(player->tradeState != TRADE_NONE && !(player->tradeState == TRADE_ACKNOWLEDGE && player->tradePartner == playerId)) {
		player->sendCancel("You are already trading.");
		return false;
	}
	else if(tradePartner->tradeState != TRADE_NONE && tradePartner->tradePartner != player->getID()) {
		player->sendCancel("This player is already trading.");
		return false;
	}

	Item* tradeItem = dynamic_cast<Item*>(internalGetThing(player, pos, 0));
	if(!tradeItem || tradeItem->getID() != itemId || !tradeItem->isPickupable()) {
		playerSendErrorMessage(player, RET_NOTPOSSIBLE);
		return false;
	}
	
	std::map<Item*, unsigned long>::const_iterator it;
	const Container* container = NULL;
	for(it = tradeItems.begin(); it != tradeItems.end(); it++) {
		if(tradeItem == it->first || 
			((container = dynamic_cast<const Container*>(tradeItem)) && container->isHoldingItem(it->first)) ||
			((container = dynamic_cast<const Container*>(it->first)) && container->isHoldingItem(tradeItem)))
		{
			player->sendTextMessage(MSG_INFO, "This item is already beeing traded.");
			return false;
		}
	}

	Container* tradeContainer = dynamic_cast<Container*>(tradeItem);
	if(tradeContainer && tradeContainer->getItemHoldingCount() + 1 > 100){
		player->sendTextMessage(MSG_INFO, "You can not trade more than 100 items.");
		return false;
	}

	player->tradePartner = playerId;
	player->tradeItem = tradeItem;
	player->tradeState = TRADE_INITIATED;
	tradeItem->useThing2();
	tradeItems[tradeItem] = player->getID();

	player->sendTradeItemRequest(player, tradeItem, true);

	if(tradePartner->tradeState == TRADE_NONE){
		std::stringstream trademsg;
		trademsg << player->getName() <<" wants to trade with you.";
		tradePartner->sendTextMessage(MSG_INFO, trademsg.str().c_str());
		tradePartner->tradeState = TRADE_ACKNOWLEDGE;
		tradePartner->tradePartner = player->getID();
	}
	else {
		Item* counterOfferItem = tradePartner->tradeItem;
		player->sendTradeItemRequest(tradePartner, counterOfferItem, false);
		tradePartner->sendTradeItemRequest(player, tradeItem, false);
	}

	return true;
}

bool Game::playerAcceptTrade(Player* player)
{
	OTSYS_THREAD_LOCK_CLASS lockClass(gameLock, "Game::playerAcceptTrade()");
	
	if(player->isRemoved())
		return false;

	player->setAcceptTrade(true);
	Player *tradePartner = getPlayerByID(player->tradePartner);
	if(tradePartner && tradePartner->getAcceptTrade()){
		Item* tradeItem1 = player->tradeItem;
		Item* tradeItem2 = tradePartner->tradeItem;

		std::map<Item*, unsigned long>::iterator it;

		it = tradeItems.find(tradeItem1);
		if(it != tradeItems.end()) {
			FreeThing(it->first);
			tradeItems.erase(it);
		}

		it = tradeItems.find(tradeItem2);
		if(it != tradeItems.end()) {
			FreeThing(it->first);
			tradeItems.erase(it);
		}
		
		bool isSuccess = false;
		ReturnValue ret1 = internalAddItem(tradeItem2->getParent(), tradeItem1, true);
		ReturnValue ret2 = internalAddItem(tradeItem1->getParent(), tradeItem2, true);

		if(ret1 == RET_NOERROR && ret2 == RET_NOERROR){
			ret1 = internalRemoveItem(tradeItem1, true);
			ret2 = internalRemoveItem(tradeItem2, true);
	
			if(ret1 == RET_NOERROR && ret2 == RET_NOERROR){
				Cylinder* cylinder1 = tradeItem1->getParent();
				Cylinder* cylinder2 = tradeItem2->getParent();

				internalMoveItem(cylinder1, cylinder2, 0, tradeItem1, tradeItem1->getItemCountOrSubtype());
				internalMoveItem(cylinder2, cylinder1, 0, tradeItem2, tradeItem2->getItemCountOrSubtype());

				isSuccess = true;
			}
		}

		player->setAcceptTrade(false);
		tradePartner->setAcceptTrade(false);
		player->sendCloseTrade();
		tradePartner->sendCloseTrade();

		if(!isSuccess){
			player->sendTextMessage(MSG_SMALLINFO, "Sorry not possible.");
			tradePartner->sendTextMessage(MSG_SMALLINFO, "Sorry not possible.");
		}

		return isSuccess;
	}

	return false;
}

bool Game::playerLookInTrade(Player* player, bool lookAtCounterOffer, int index)
{
	OTSYS_THREAD_LOCK_CLASS lockClass(gameLock, "Game::playerLookInTrade()");

	Player* tradePartner = getPlayerByID(player->tradePartner);
	if(!tradePartner)
		return false;

	Item* tradeItem = NULL;

	if(lookAtCounterOffer)
		tradeItem = tradePartner->getTradeItem();
	else
		tradeItem = player->getTradeItem();

	if(!tradeItem)
		return false;

	uint32_t lookDistance = std::sqrt(std::pow(std::abs(player->getPosition().x - tradeItem->getPosition().x), 2) +
		std::pow(std::abs(player->getPosition().y - tradeItem->getPosition().y), 2));

	if(index == 0){
		stringstream ss;
		ss << "You see " << tradeItem->getDescription(lookDistance);
		player->sendTextMessage(MSG_INFO, ss.str().c_str());
		return false;
	}

	Container* tradeContainer = dynamic_cast<Container*>(tradeItem);
	if(!tradeContainer || index > tradeContainer->getItemHoldingCount())
		return false;

	bool foundItem = false;
	std::list<const Container*> stack;
	stack.push_back(tradeContainer);
	
	ItemList::const_iterator it;

	while(!foundItem && stack.size() > 0){
		const Container *container = stack.front();
		stack.pop_front();

		for(it = container->getItems(); it != container->getEnd(); ++it){
			Container *container = dynamic_cast<Container*>(*it);
			if(container){
				stack.push_back(container);
			}

			--index;
			if(index == 0){
				tradeItem = *it;
				foundItem = true;
				break;
			}
		}
	}
	
	if(foundItem){
		stringstream ss;
		ss << "You see " << tradeItem->getDescription(lookDistance);
		player->sendTextMessage(MSG_INFO, ss.str().c_str());
	}

	return foundItem;
}

void Game::playerCloseTrade(Player* player)
{
	OTSYS_THREAD_LOCK_CLASS lockClass(gameLock, "Game::playerCloseTrade()");
	
	Player* tradePartner = getPlayerByID(player->tradePartner);

	std::vector<Item*>::iterator it;
	if(player->getTradeItem()) {
		std::map<Item*, unsigned long>::iterator it = tradeItems.find(player->getTradeItem());
		if(it != tradeItems.end()) {
			FreeThing(it->first);
			tradeItems.erase(it);
		}
	}

	player->setAcceptTrade(false);
	player->sendTextMessage(MSG_SMALLINFO, "Trade cancelled.");
	player->sendCloseTrade();

	if(tradePartner) {
		if(tradePartner->getTradeItem()) {
			std::map<Item*, unsigned long>::iterator it = tradeItems.find(tradePartner->getTradeItem());
			if(it != tradeItems.end()) {
				FreeThing(it->first);
				tradeItems.erase(it);
			}
		}

		tradePartner->setAcceptTrade(false);
		tradePartner->sendTextMessage(MSG_SMALLINFO, "Trade cancelled.");
		tradePartner->sendCloseTrade();
	}
}

bool Game::playerLookAt(Player* player, const Position& pos, uint16_t itemId, uint8_t stackpos)
{
	OTSYS_THREAD_LOCK_CLASS lockClass(gameLock, "Game::playerCloseTrade()");

	Thing* thing = internalGetThing(player, pos, 0);
	if(!thing){
		playerSendErrorMessage(player, RET_NOTPOSSIBLE);
		return false;
	}

	uint32_t lookDistance = 0;
	if(thing == player)
		lookDistance = -1;
	else{
		const Position& LookPos = player->getPosition();
		const Position& thingMapPos = thing->getPosition();
		
		if(LookPos.z != thingMapPos.z)
			lookDistance = std::abs(LookPos.z - thingMapPos.z) * 2;
		else
			lookDistance = std::sqrt( std::pow(std::abs(LookPos.x - thingMapPos.x), 2) +
			std::pow(std::abs(LookPos.y - thingMapPos.y), 2));
	}

	std::stringstream ss;
	ss << "You see " << thing->getDescription(lookDistance);
	player->sendTextMessage(MSG_INFO, ss.str().c_str());

	return true;

}

/*
void Game::checkCloseAttack(Player* player, Creature* target)
{
  if((std::abs(player->getPosition().x - target->getPosition().x) > 7) ||
		 (std::abs(player->getPosition().y - target->getPosition().y) > 5) ||
	   (player->getPosition().z != target->getPosition().z)){
	  player->sendTextMessage(MSG_SMALLINFO, "Target lost.");
	  playerSetAttackedCreature(player, 0);
  } 
}
*/

void Game::playerSetAttackedCreature(Player* player, unsigned long creatureid)
{
	OTSYS_THREAD_LOCK_CLASS lockClass(gameLock, "Game::playerSetAttackedCreature()");
		
	if(player->isRemoved())
		return;

	if(player->attackedCreature != 0 && creatureid == 0) {
		player->sendCancelAttacking();
	}

	Creature* attackedCreature = NULL;
	if(creatureid != 0) {
		attackedCreature = getCreatureByID(creatureid);
	}

	if(!attackedCreature || (attackedCreature->access != 0 || (getWorldType() == WORLD_TYPE_NO_PVP && player->access == 0 && dynamic_cast<Player*>(attackedCreature)))) {
		if(attackedCreature) {
			player->sendTextMessage(MSG_SMALLINFO, "You may not attack this player.");
		}

		player->sendCancelAttacking();
		player->setAttackedCreature(NULL);
		stopEvent(player->eventCheckAttacking);
		player->eventCheckAttacking = 0;
	}
	else if(attackedCreature) {
		player->setAttackedCreature(attackedCreature);
		stopEvent(player->eventCheckAttacking);
		player->eventCheckAttacking = addEvent(makeTask(2000, std::bind2nd(std::mem_fun(&Game::checkCreatureAttacking), player->getID())));
	}
	
}

bool Game::requestAddVip(Player* player, const std::string &vip_name)
{
	OTSYS_THREAD_LOCK_CLASS lockClass(gameLock, "Game::requestAddVip");
	std::string real_name;
	real_name = vip_name;
	unsigned long guid;
	unsigned long access_lvl;
	
	if(!IOPlayer::instance()->getGuidByName(guid, access_lvl, real_name)){
		player->sendTextMessage(MSG_SMALLINFO, "A player with that name doesn't exist.");
		return false;
	}
	if(access_lvl > player->access){
		player->sendTextMessage(MSG_SMALLINFO, "You can not add this player.");
		return false;
	}
	bool online = (getPlayerByName(real_name) != NULL);
	return player->addVIP(guid, real_name, online);
}

void Game::flushSendBuffers()
{	
	OTSYS_THREAD_LOCK_CLASS lockClass(gameLock, "Game::flushSendBuffers()");

	for(std::vector<Player*>::iterator it = BufferedPlayers.begin(); it != BufferedPlayers.end(); ++it) {
		(*it)->flushMsg();
		(*it)->SendBuffer = false;
		(*it)->releaseThing2();
/*
#ifdef __DEBUG__
		std::cout << "flushSendBuffers() - releaseThing()" << std::endl;
#endif
*/
		}	
	BufferedPlayers.clear();
	
	//free memory
	for(std::vector<Thing*>::iterator it = ToReleaseThings.begin(); it != ToReleaseThings.end(); ++it){
		(*it)->releaseThing2();
	}
	ToReleaseThings.clear();
	
	
	return;
}

void Game::addPlayerBuffer(Player* p)
{		
	OTSYS_THREAD_LOCK_CLASS lockClass(gameLock, "Game::addPlayerBuffer()");

/*
#ifdef __DEBUG__
	std::cout << "addPlayerBuffer() - useThing()" << std::endl;
#endif
*/
	if(p->SendBuffer == false){
		p->useThing2();
		BufferedPlayers.push_back(p);
		p->SendBuffer = true;
	}
	
	return;
}

void Game::FreeThing(Thing* thing)
{	
	OTSYS_THREAD_LOCK_CLASS lockClass(gameLock, "Game::FreeThing()");
	//std::cout << "freeThing() " << thing <<std::endl;
	ToReleaseThings.push_back(thing);
	
	return;
}
