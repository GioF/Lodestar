#include <list>
#include <mutex>
#include <utility>
#include <boost/interprocess/sync/interprocess_semaphore.hpp>

using semaphore = boost::interprocess::interprocess_semaphore;

namespace Lodestar{
    template <class listType>
    class ManagedList{
        public:
            ManagedList(): awaitSignal(0), waitingSignal(0), continueSignal(0){};

            int nSignals;
            semaphore awaitSignal;
            semaphore waitingSignal;
            semaphore continueSignal;

            std::list<listType> list;
            std::mutex listLock;

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
             * @returns if deletion process should start.
             * */
            virtual bool deletionHeuristic() = 0;

            /**
             * Signals threads to stop operating on this list and calls deletionFunction()
             * if deletionHeuristic() returns true.
             * 
             * Will send [nSignals] signals via awaitSignal so that threads operating
             * on this list are notified to wait, then wait for them to notify they are
             * waiting; once it's done, calls deletionFunction() to delete entries, then
             * and sends [nSignals] signals via continueSignal to notify that deletion is done.
             * To prevent concurrent write access to list, locks listLock and unlocks it once
             * finished.
             * */
            void cleanList(){
                if(deletionHeuristic()){
                    listLock.lock();
                    for(int n = 0; n < nSignals; n++)
                        awaitSignal.post();
                    
                    for(int n = 0; n < nSignals; n++)
                        waitingSignal.wait();

                    deletionFunction();
                    
                    for(int n = 0; n < nSignals; n++)
                        continueSignal.post();
                    listLock.unlock();
                }
            }
    };
}
