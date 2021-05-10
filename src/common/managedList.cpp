#include <list>
#include <mutex>
#include <utility>
#include <atomic>
#include <chrono>
#include <thread>
#include <boost/interprocess/sync/interprocess_semaphore.hpp>

using semaphore = boost::interprocess::interprocess_semaphore;

namespace Lodestar{
    template <class listType>
    class ManagedList{
        public:
            ManagedList(): awaitSignal(0), waitingSignal(0), continueSignal(0), stopSignal(0){};

            bool isOk;

            std::list<std::thread*> threadList;
            std::mutex threadLock;     ///< mutex to control thread starting and stopping
            std::atomic<int> nSignals;
            semaphore awaitSignal;
            semaphore waitingSignal;
            semaphore continueSignal;
            semaphore stopSignal;      ///< signal to stop a list manager from executing

            std::list<listType> list;
            std::mutex listLock;      ///< mutex to control list addition

            /**
             * Calls list.remove_if to delete items that have a false active property.
             *
             * When reimplementing it, remember to call list.remove_if since
             * this behaviour is expected.
             * */
            virtual void deletionFunction(){
                    list.remove_if([](listType item){return !item.active;});
            }

            /**
             * Function called to decide if deletion process should take place or not.
             *
             * This function should only return true at the when cleaning
             * the list up will be a net benefit, so define this heuristic
             * cleverly (but in a way that can still be read by poor souls later)
             * 
             * @returns if deletion process should start.
             * */
            virtual bool deletionHeuristic() = 0;

            /**
             * Function called to decide if new threads should be spawned.
             *
             * This is used as a way to scale the amount of workers managing the list,
             * launching [the returned amount] new threads if positive, and stopping
             * [the returned amount] threads if negative.
             * */
            //virtual int threadHeuristic() = 0;

            /**
             * Function called to iterate over list and manage its entries.
             *
             * Do not make this function loop; it is used inside a loop until it is sent
             * a signal to stop due to it not being needed anymore.
             * */
            virtual void manage() = 0;

            /**
             * Calls manage() until it is sent a signal to stop.
             *
             * To safeguard against starting or stopping the manage() loop while
             * cleanList() executes, it uses threadLock() before incrementing or
             * decrementing nSignals. This probably isn't safe.
             * */
            void iterate(){
                threadLock.lock();
                nSignals++;
                threadLock.unlock();

                while(!stopSignal.try_wait()){
                    //check if there is a signal to wait for deletion
                    if(awaitSignal.try_wait()){
                        //notify deletion thread that this thread is waiting
                        waitingSignal.post();
                        //wait until deletion thread signals to continue
                        continueSignal.wait(); 
                    }

                    manage();
                }

                if(threadLock.try_lock()){
                    nSignals--;
                    threadLock.unlock();
                }else{
                    //behave as a waiting thread (so that
                    //cleanList() functions correctly), then
                    //subtract number of running threads
                    awaitSignal.wait();
                    waitingSignal.post();
                    continueSignal.wait();

                    threadLock.lock();
                    nSignals--;
                    threadLock.lock();
                }
            }
            
            /**
             * Signals threads to stop operating on this list and calls deletionFunction()
             * if deletionHeuristic() returns true.
             * 
             * Will send [nSignals] signals via awaitSignal so that threads operating
             * on this list are notified to wait, then wait for them to notify they are
             * waiting; once it's done, calls deletionFunction() to delete entries, then
             * and sends [nSignals] signals via continueSignal to notify that deletion is done.
             *
             * To prevent concurrent write access to list or thread creation durint list cleanup,
             * locks listLock and threadLock, then unlocks them once done.
             * */
            void cleanList(){
                if(deletionHeuristic()){
                    listLock.lock();
                    threadLock.lock();

                    for(int n = 0; n < nSignals; n++)
                        awaitSignal.post();
                    
                    for(int n = 0; n < nSignals; n++)
                        waitingSignal.wait();

                    deletionFunction();
                    
                    for(int n = 0; n < nSignals; n++)
                        continueSignal.post();

                    threadLock.unlock();
                    listLock.unlock();
                }
            }

            /**
             * Oversees the managedList by calling cleanList(), then scaling up or
             * down amount of running threads iterating through this list, and then
             * sleeping for 500 milliseconds.
             *
             * Since this function blocks until the object is being destructed, it's
             * best to launch it in a standalone thread.
             * */
            void oversee(){
                int nNewThreads = 0;

                while(isOk){
                    cleanList();
                    //will uncomment once threadHeuristic is implemented
                    //nNewThreads = threadHeuristic();

                    if(nNewThreads > 0){
                        //start [nNewThreads] new threads
                        while(nNewThreads > 0){
                            auto newThread = new std::thread(&ManagedList::iterate, this);
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

                    // NOTE: should only sleep when executing asynchronously
                    std::this_thread::sleep_for(std::chrono::milliseconds(500));
                }
            }
    };
}
