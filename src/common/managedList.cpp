#ifndef LODEMLIST_H
#define LODEMLIST_H
#include <list>
#include <mutex>
#include <utility>
#include <atomic>
#include <chrono>
#include <thread>
#include <future>
#include <boost/interprocess/sync/interprocess_semaphore.hpp>

using semaphore = boost::interprocess::interprocess_semaphore;
using namespace std::chrono_literals;

namespace Lodestar{
    /**
     * An abstract class to implement managed lists with.
     *
     * It can be either used synchronously - in which the program needs to call spin()
     * regularly to manage said lists by calling manage() and then cleaning the list with
     * cleanList(), or asynchronously by dynamically starting new threads according to the
     * defined thread heuristics.
     *
     * If only used synchronously, only manage(), deletionHeuristic(), and deletionFunction()
     * need to be properly implemented.
     *
     * If used asynchronously, threadHeuristic() also needs to be properly implemented.
     * */
    template <class listType>
    class ManagedList{
        public:
            /**
             * Simple constructor made to manage the list synchronously by calling spin().
             * */
            ManagedList(){};

            /**
             * Constructor to be used when managing list asynchronoously via threads.
             *
             * @param nMaxThreads the maximum amount of threads that this ManagedList can
             * have at once.
             * */
            ManagedList(long nMaxThreads): maxThreads(nMaxThreads){
                isAsync = true;
            }

            ~ManagedList(){
                if(isAsync){
                    //stop overseer thread
                    isOk = false;
                    if(overseerThread.joinable())
                       overseerThread.join();

                    //tell all threads to stop
                    while(nThreads > 0){
                        stopSignal.post();
                        nThreads--;
                    }

                    //join them all
                    for(auto it = threadList.begin(); it != threadList.end(); it++){
                        it->wait();
                    }
                }
            }

            bool isAsync;
            int maxThreads = 0;
            int nThreads = 0;

            std::list<listType> list;
            std::mutex listLock;      ///< mutex to control list addition

            /**
             * Function called to start overseer thread.
             *
             * @param sleepTime the time that the thread should sleep
             * between oversee() calls.
             * */
            void init(std::chrono::milliseconds sleepTime){
                overseerThread = std::thread([this, sleepTime](){
                    oversee();
                    std::this_thread::sleep_for(sleepTime);
                });
            }

            /**
             * Iterates once over list by using manage() then cleans it.

             * */
            void spin(){
                if(isAsync){
                    iterate();
                    cleanList();
                }
            }

        protected:
            std::thread overseerThread;

        private:
            friend class test_managed;

            bool isOk;

            std::list<std::future<void>> threadList;
            std::mutex threadLock;     ///< mutex to control thread starting and stopping

            semaphore awaitSignal = 0;
            semaphore waitingSignal = 0;
            semaphore continueSignal = 0;
            semaphore stopSignal = 0;      ///< signal to stop a list manager from executing

            /**
             * Function to remove certain entries on the list (by default, entries with
             * a false [active] property).
             *
             * Called when spin() or oversee() is called, so as long as spinning regularly
             * or operating asynchronously it isn't necessary to manually call it.
             * */
            virtual void deletionFunction(){
                    list.remove_if([](listType item){return !item.active;});
            }

            /**
             * Function called to decide when deletionFunction() should be called.
             *
             * This function should only return true at the when cleaning
             * the list up will be a net benefit, so define this heuristic
             * cleverly (but in a way that can still be read by poor souls later)
             * 
             * @returns if deletion process should start.
             * */
            virtual bool deletionHeuristic() = 0;

            /**
             * Function that determines how many threads should be running at the moment.
             *
             * This is used as a way to scale the amount of workers managing the list,
             * launching [the returned amount - nThreads] new threads if returned value
             * is greater than the amount of currently running threads, and stopping
             * [the returned amount - nThreads] threads if returned value is lower than
             * amount of currently running threads.
             * */
            virtual int threadHeuristic() = 0;

            /**
             * Function called to iterate over list and manage its entries.
             *
             * Do not make this function loop; it is used inside a loop until it is sent
             * a signal to stop due to it not being needed anymore.
             * */
            virtual void manage() = 0;

            // NOTE: This is probably an unsafe function, check this later
            /**
             * Calls manage() until it is sent a signal to stop.
             *
             * Before entering the loop that calls manage(), increments nThreads
             * so that the amount of threads operating on this list is known.
             * 
             * To safeguard against changing nThreads while cleanList() executes,
             * it uses threadLock() before incrementing or decrementing nThreads.
             * This probably isn't safe, so i'll look at it later.
             * */
            void iterate(){
                threadLock.lock();
                nThreads++;
                threadLock.unlock();

                while(!stopSignal.try_wait()){
                    if(awaitSignal.try_wait()){ //check if there is a signal to wait for deletion
                        waitingSignal.post();   //notify deletion thread that this thread is waiting
                        continueSignal.wait();  //wait until deletion thread signals to continue
                    }

                    manage();
                }

                if(threadLock.try_lock()){
                    nThreads--;
                    threadLock.unlock();
                }else{
                    //need to behave as a waiting thread
                    //(so that cleanList() functions correctly)
                    awaitSignal.wait();
                    waitingSignal.post();
                    continueSignal.wait();

                    threadLock.lock();
                    nThreads--;
                    threadLock.unlock();
                }
            }
            
            /**
             * Signals threads to stop operating on this list and calls deletionFunction()
             * if deletionHeuristic() returns true.
             * 
             * Will send [nThreads] signals via awaitSignal so that threads operating
             * on this list are notified to wait, then wait for them to notify they are
             * waiting; once it's done, calls deletionFunction() to delete entries, then
             * and sends [nThreads] signals via continueSignal to notify that deletion is done.
             *
             * To prevent concurrent write access to list or thread creation durint list cleanup,
             * locks listLock and threadLock, then unlocks them once done.
             * */
            void cleanList(){
                if(deletionHeuristic()){
                    listLock.lock();
                    threadLock.lock();

                    for(int n = 0; n < nThreads; n++)
                        awaitSignal.post();
                    
                    for(int n = 0; n < nThreads; n++)
                        waitingSignal.wait();

                    deletionFunction();
                    
                    for(int n = 0; n < nThreads; n++)
                        continueSignal.post();

                    threadLock.unlock();
                    listLock.unlock();
                }
            }

            /**
             * Oversees the managedList by calling cleanList(), then scaling up or
             * down amount of running threads iterating through this list via
             * threadHeuristics()
             * */
            void oversee(){
                int nNewThreads = 0;

                cleanList();
                nNewThreads = threadHeuristic() - nThreads;
                
                if(nNewThreads > 0 && nThreads <= maxThreads){
                    //start [nNewThreads] new threads
                    while(nNewThreads > 0){
                        auto nThread = std::async(&ManagedList::iterate, this);
                        threadList.push_front(std::move(nThread));
                        nNewThreads--;
                    }
                }else if(nNewThreads < 0){
                    //send [-nNewThreads] stopSignals to scale down
                    //amount of running threads
                    while(nNewThreads < 0){
                        stopSignal.post();
                        nNewThreads++;
                    }
                }

                //clean threads that are inactive
                threadList.remove_if([](std::future<void>& t){
                    return t.wait_for(1ms) == std::future_status::ready;
                });
            }
    };
}
#endif
