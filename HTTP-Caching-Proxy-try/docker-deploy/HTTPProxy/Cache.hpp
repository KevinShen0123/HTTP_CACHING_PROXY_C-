#include <map>
#include <string>
#include <cstdlib>
#include <iostream>
#include <thread>
#include "parser.hpp"

class Cache{
private:
/**
 * @param cache_map key is hostname+target from requests
 * @param capacity number of items that can be stored
 * @param used_list least recently used item -> most recently used item
 * @param rwlock read/write lock
*/
    std::map<std::string, http::response<http::dynamic_body> > cache_map;
	int capacity;
	std::vector<std::string> used_list; 
	pthread_rwlock_t rwlock = PTHREAD_RWLOCK_INITIALIZER;
	pthread_mutex_t * loglock;
	std::ofstream * LogStream;

	/**
	 * this method remove the least used item from the object
	*/
	void evict(){
		pthread_rwlock_wrlock(&rwlock);
		std::string key = used_list[0];
		cache_map.erase(key);
		used_list.erase(used_list.begin());
		capacity++;
		pthread_rwlock_unlock(&rwlock);
		pthread_mutex_lock(loglock);
		*LogStream<<"(no-id): NOTE evicted \""<< key<<"\" from cache" <<std::endl;
		pthread_mutex_unlock(loglock);
	}

public:
    Cache(int m, pthread_mutex_t * ll, std::ofstream * s):capacity(m), loglock(ll), LogStream(s){}

	/**
	 * whether the key in in the cache
	 * @param key the key of the map
	 * @return true if in cache; false if not
	*/
	bool isInCache(std::string & key){
		pthread_rwlock_rdlock(&rwlock);
		bool result = cache_map.find(key) != cache_map.end();
		pthread_rwlock_unlock(&rwlock);

        return result;
    }

	/**
	 * update one key in cache
	 * @param key key in the map
	 * @param response response to store in the cache
	*/
	int update(std::string & key, http::response<http::dynamic_body> response){
		if(isInCache(key)){
			pthread_rwlock_wrlock(&rwlock);
			cache_map[key] = response;
			pthread_rwlock_unlock(&rwlock);
			return 1;
		} 
		return 0;
	}
	/**
	 * return the reponse stored in cache, update the LRU list
	 * @param key the key to get
	 * @return NULL if not in cache; reponse stored in cache
	*/
	http::response<http::dynamic_body> * get(std::string & key){
		if(!isInCache(key)){
			return NULL;
		}
		//update the used_list
		pthread_rwlock_wrlock(&rwlock);
		for(int i = 0; i < used_list.size(); i++){
			if(used_list[i].compare(key) == 0){
				//update used_list if not alreadly the newest
				if(i!=used_list.size()-1){
					used_list.erase(used_list.begin()+ i);
					used_list.push_back(key);
				}
			}
		}
		pthread_rwlock_unlock(&rwlock);
		return &cache_map[key];
	}
	
	/**
	 * insert a item into the cache
	 * @param key 
	 * @param reponse value
	 * @return 1 if success, 0 if not
	*/
	int put(std::string key, http::response<http::dynamic_body> response){
		//already in cache, do not store
		if(isInCache(key)){
			return 0;
		}
		if(capacity == 0){
			evict();
		}else{
			pthread_rwlock_wrlock(&rwlock);
			cache_map[key] = response;
			capacity--;
			used_list.push_back(key);
			pthread_rwlock_unlock(&rwlock);
			return 1;
		}
		return 0; 
	}
}; 