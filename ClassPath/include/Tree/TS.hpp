#ifndef hpp_CPP_TSTree_CPP_hpp
#define hpp_CPP_TSTree_CPP_hpp

// Need comparable declaration
#include "Comparable.hpp"
// Need FIFO for iterator
#include "../Container/FIFO.hpp"
// We need Bool2Type
#include "../Container/Bool2Type.hpp"
// We need containers to store search results
#include "../Container/Container.hpp"

#ifdef _MSC_VER
#pragma warning(push)
// We don't want the debug symbol name length > 256 errors
#pragma warning(disable: 4251)
#pragma warning(disable: 4786)
#endif

/** The Tree namespace holds the classes for tree manipulation */
namespace Tree
{
    /** The Ternary search tree is used to store dictionary with almost the same
        performance as hash map, but allowing approximate search in the tree, and using a lot less storage.    */
    namespace TernarySearch
    {
        using namespace ::Comparator;
         /** A node index, must be unsigned so -1 is mapped to the biggest possible value */
        typedef uint32  NodeIndex;
        /** The bad node index value */
        const NodeIndex badIndex = (NodeIndex)-1;


        /** Default deletion for node's objects doing nothing (no deletion) */
        template <typename T, typename KeyType>
        struct NoDeletion
        {    inline static void deleter(T *, KeyType) {} };

        /** Default true deletion for node's objects  */
        template <typename T, typename KeyType>
        struct DeletionWithDelete
        {    inline static void deleter(T *& t, KeyType) { delete t; t = 0; } };

        /** The transformer interface transform a key to make searching easier.
            Transformations may be accented char -> plain char / any case->lowercase.
            Please note that those transformation have a meaning on alphabetic language.

            Using a transform key is usually done by a table lookup of size 2^[sizeof(KeyType) * 8]
        */
        template <class KeyType>
        struct TransformKey
        {
            /** The key simplification process */
            static KeyType simplifyKey(const KeyType initialKey);
        };

        /** The identity transformer interface gives the same output as the input.
            @sa TransformKey
        */
        template <class KeyType>
        struct NoTransformKey
        {
            /** The key simplification process */
            inline static KeyType simplifyKey(const KeyType initialKey) { return initialKey; }
        };

        /** The node is used to store the data and the optimized path to it.
            In memory, they are stored in a contiguous array like this:
            @verbatim
            [   Node 0    |    Node 1 |    Node 2   |  Node 3   | Node 4    | etc...  ]
            [p:-1, L, H   | p: 0, L, H| p: 1, L, H  | p:0, L, H | p:0, L, H |         ]
                   \--|------------------------------^
                      \------------------------------------------^
            @endverbatim
            This makes the following tree:
            @verbatim
                 N0
               / | \
              N3 N1 N4
                 |
                 N2
            @endverbatim
            The middle index is always the next node in the array. */
        template <typename T, typename KeyType>
        struct Node
        {
            /** The direction selection while iterating */
            enum { Lower = 0, Equal = 1, Higher = 2 };
            /** The node state */
            enum State { EndNode = 1, PathNode = 2, EmptyNode = 0 };

            /** The split char for this node */
            KeyType     splitChar;
            /** The state for this node */
            uint8       state;

            /** The data union */
            union
            {
                /** The index to the lower element */
                NodeIndex   lower;
                /** The data itself, the node doesn't manage it, but the tree does */
                T *         data;
            };
            /** The index to the higher element */
            NodeIndex   higher;
            /** The index to the parent (used for iterative algorithm) element */
            NodeIndex   parent;

            /** Is this node an end node ? */
            inline bool isEnd() const { return (state == EndNode); }
        };

        /** It's used to compute the current balance in the tree */
        template <typename T,typename KeyType>
        struct NodeInfo
        {
            NodeIndex           index;
            Node<T, KeyType> *  node;

            int  height;
            int  balance;
            int  higherBalance;
            int  lowerBalance;
            bool balanceDone;

            NodeInfo(const NodeIndex index = badIndex, Node<T, KeyType> * node = 0)
                : index(index), node(node), height(0), balance(0), higherBalance(0), lowerBalance(0), balanceDone(false) {}
        };

        /** The iterator for the ternary tree */
        template <typename T, typename KeyType>
        struct EndIterator
        {
        public:
            /** The node type */
            typedef Node<T, KeyType>  NodeT;

        private:
            /** The array pointer */
            NodeT const *           array;
            /** The number of items in the array */
            uint32                  count;
            /** The iterator current index */
            NodeIndex               current;
            /** The iterator start index */
            NodeIndex               start;
            /** The iterator type (if true, the iterator walk up indefinitely) */
            bool                    seeAll;

            // Operators
        public:
            /** Data access operator */
            T * operator * () { return array[current].isEnd() ? array[current].data : 0; }
            /** Simple increment operator */
            const EndIterator & operator++() { if (isValid()) goNextEndNode(); return *this; }

            // Interface
        public:
            /** Go to the next end node */
            inline bool goNextEndNode()
            {
                if (array[current].isEnd())
                {
                    // Check for all end node that could be below this end node (like for mum(end node) but prefix of mummy(end node))
                    if (array[current].higher < count && array[array[current].higher].parent == current) current = array[current].higher;
                    else
                    {
                        // We need to iterate up to the start node, and as soon as we can find a higher node than us, we iterate down to its lower end path (except us)
                        // The process can be described as a selective bottom-up then top-down approach

                        // So, let's start the bottom up approach
                        while (1)
                        {
                            NodeIndex previous = current;
                            current = array[current].parent;
                            if (!seeAll && current == start) { current = badIndex; break; }
                            if (current == badIndex) break;
                            if (!array[current].isEnd() && /* Next node */ (current+1) != previous && previous != array[current].higher) { current++; break; }
                            if (array[current].higher != badIndex && array[current].higher != previous) { current = array[current].higher; break;}
                        }

                        // No other node found from the bottom up approach
                        if (current == badIndex) return false;
                    }
                }

                // Then try the top down approach
                while (1)
                {
                    if (array[current].isEnd()) return true;
                    if (array[current].lower != badIndex) { current = array[current].lower; }
                    else current++;

                    // If the tree is ill formed, the code will crash here
                }
                // We should never reach this point
                return false;
            }

            /** If the node is an end node, return the key path to the node in the given buffer
                @param keyArray     the key array to write the key into
                @param arraySize    the key array allocated size
                @return The required key array length (if the specified arguments are invalid, the return is still valid)
                @warning If a TransformKey has been used, the returned key is the transformed keys
            */
            uint32 getKeyName(KeyType * keyArray, uint32 arraySize) const
            {
                if (!array[current].isEnd()) return 0;
                uint32 minKeyLength = computeKeyLength();
                if (!keyArray || arraySize < minKeyLength) return minKeyLength;

                uint32 length = 0;
                NodeIndex cur = current;
                bool ignoreNextOne = false;
                while (cur != badIndex)
                {
                    if (!ignoreNextOne) {  length++; keyArray[minKeyLength - length] = array[cur].splitChar; }
                    if (array[cur].parent != (cur - 1) || cur == 0 || array[cur - 1].isEnd()) { ignoreNextOne = true; }  else ignoreNextOne = false;
                    cur = array[cur].parent;
                }

                return minKeyLength;
            }

            /** Get the required key length to reach this node
                @return the key length in key count */
            uint32 computeKeyLength() const
            {
                uint32 length = current != badIndex;
                NodeIndex cur = current;
                while (cur != badIndex) { if (array[cur].parent == (cur - 1) && cur > 0 && !array[cur-1].isEnd()) length++; cur = array[cur].parent; }
                return length;
            }

            /** Check if this iterator is valid */
            inline bool isValid() const { return current != badIndex; }
            /** Reset the iterator to its first position */
            inline void Reset() { current = array ? (array[start].isEnd() ? start : start + 1) : badIndex; }

            /** Create an EndIterator */
            EndIterator(const uint32 _count = 0, NodeT const * _array = 0, NodeIndex _start = badIndex, const bool visitAll = false)
                : array(_array), count(_count), current(_start == badIndex ? badIndex : (_array ? (_array[_start].isEnd() ? _start : (_start+1)) : badIndex)), start(_start), seeAll(visitAll) {}
        };

        /** The Levenshtein search result stack */
        template <typename T, typename KeyType>
        struct LevIterator
        {
        public:
            /** The node type */
            typedef Node<T, KeyType>  NodeT;

            // Members
        private:
            /** The result index list */
            Container::PlainOldData<NodeIndex>::ChainedList results;
            /** The array (for retrieving the nodes) */
            NodeT const *     array;
            /** The currently parsed node */
            NodeIndex  *      current;

            // Operators
        public:
            /** Dereference operator */
            T * operator * () { return array[*current].isEnd() ? array[*current].data : 0; }
            /** Equal operator */
            inline LevIterator & operator = (const LevIterator & other)
            {
                array = other.array;
                results = other.results;
                Reset();
                return *this;
            }

            // Interface
        public:
            /** Add a valid result to the list.
                This method check if the given result already exists in the list before inserting it (it doesn't insert already present results)
                @param index the node index to add
                @return true on success, false on failure (or already present in the list) */
            bool appendIndex(const NodeIndex index)
            {
                // First check if the index is already in the list
                if (results.indexOf(index) == results.ChainedError)
                    return results.Add(index);
                return false;
            }

            /** Add valid results to the list.
                This method check if the given result already exists in the list before inserting it (it doesn't insert already present results)
                @param iter the other iterator to merge with
                @return true on success, false on failure (or already present in the list) */
            bool appendIter(const LevIterator & iter)
            {
                // First check if the index is already in the list
                NodeIndex * index = iter.results.parseList(true);
                while (index != NULL)
                {
                    if (results.indexOf(*index) == results.ChainedError)
                    {
                        if (!results.Add(*index)) return false;
                    }
                    index = iter.results.parseList();
                }
                return true;
            }

            /** Check if this iterator is valid */
            inline bool isValid() const { return array && current; }
            /** Reset the iterator to its first position */
            inline void Reset() { current = results.parseList(true); }

            /** Go to the next end node */
            inline bool goNextEndNode()
            {
                // Make sure the current is valid first
                if (!current)   Reset();
                else            current = results.parseList();
                return current != 0;
            }

            /** If the node is an end node, return the key path to the node in the given buffer
                @param keyArray     the key array to write the key into
                @param arraySize    the key array allocated size
                @return The required key array length (if the specified arguments are invalid, the return is still valid)
                @warning If a TransformKey has been used, the returned key is the transformed keys
            */
            uint32 getKeyName(KeyType * keyArray, uint32 arraySize) const
            {
                if (!isValid() || !array[*current].isEnd()) return 0;
                uint32 minKeyLength = computeKeyLength();
                if (!keyArray || arraySize < minKeyLength) return minKeyLength;

                uint32 length = 0;
                NodeIndex cur = *current;
                bool ignoreNextOne = false;
                while (cur != badIndex)
                {
                    if (!ignoreNextOne) {  length++; keyArray[minKeyLength - length] = array[cur].splitChar; }
                    if (array[cur].parent != (cur - 1) || cur == 0 || array[cur - 1].isEnd()) { ignoreNextOne = true; }  else ignoreNextOne = false;
                    cur = array[cur].parent;
                }

                return minKeyLength;
            }

        private:
            /** Get the required key length to reach this node
                @return the key length in key count */
            uint32 computeKeyLength() const
            {
                uint32 length = *current != badIndex;
                NodeIndex cur = *current;
                while (cur != badIndex) { if (array[cur].parent == (cur - 1) && cur > 0 && !array[cur-1].isEnd()) length++; cur = array[cur].parent; }
                return length;
            }

            // Construction
        public:
            /** Default constructor */
            LevIterator(NodeT * _array) : array(_array), current(0) {}

            /** Append a previous result (strictly speaking, it moves the other member) */
            LevIterator(const LevIterator & other) : array(other.array), current(other.current) { results.moveAppendedList(const_cast<LevIterator &>(other).results); }
        };

        /** The Ternary search tree is used to store dictionary with almost the same
            performance as hash map, but allowing approximate search in the tree.

            The memory consumption is a higher than an hash map still.
            A node in the tree usually takes between 14 and 18 bytes, and there is one node per distinct key unit.

            This tree's methods are all iterative unless specified in the documentation.

            A Ternary tree will store such words (bad, bee, bob, bud, bump, butter) like:
            @verbatim
                    b
                    |
                    o
                  / | \
                 /  b  \
                a      u
                |\     |
                d e    t
                  |   /|          / stand for lower node (where the lower key replaces the current key)
                  e  m t          | stand for next node (append the key to the word)
                    /| |          \ stand for higher node (where the higher key replaces the current key)
                   d p e
                       |
                       r
            @endverbatim
            In the previous example, the minimum required memory is 15 nodes * 14 bytes = 210 bytes.
            A hash holding all those keys will require at least 7 buckets (of pointers), 28 bytes for storing the keys = 56 bytes.
            The difference between both structure will reduce when the number of stored items increase.

            The tree takes ownership of data passed in
            @param T            The data type to store in the tree (it's the pointed type not the type itself (ie MyObject not MyObject *), because it's a pointer in each node)
            @param KeyType      The basic char used (usually "char" or "wchar_t" for unicode)
            @param Policy       The comparator policy (for the basic key's like char or wchar_t, it's best to let the default comparator)
            @param Transform    The key transformer (this is used to modify the key value for better search results.
                                                     For example, using TransformKey<wchar_t>, accented char are replaced by the non accented char, etc...
                                                     Use NoTransformKey for no transformation)
            @param DeleterT     The deleter policy (this is used to delete the T object when it's no longer used. Possible value are DeletionWithDelete, NoDeletion)

            @sa DeletionWithDelete, NoDeletion, TransformKey,  NoTransformKey
         
            @warning To avoid storing pointers for each node, we used a node array and each node refers to each other with index in this array.
                     This is very compact and cache friendly, but the only downside is that you can't delete a node without a high cost (not O(log N) anymore, but O(N) at worst) since it
                     would require moving and fixing all the nodes after the deleted nodes to avoid holes in the array.
                     The approach used here is to simply mark a node leading to a deleted key as deleted and increase the deletion count (lazy deletion).
                     It's up to the caller using deleteInTree() to check the deletion count and call reassemble() later one. This will rebuild a new Tree in O(N).
        */
        template <typename T, typename KeyType, class Policy = ::Comparator::Comparable<KeyType, Comparator::DefaultComparator>, class Transform = TransformKey<KeyType>, typename DeleterT = DeletionWithDelete<T, KeyType> >
        class Tree
        {
            // Types used by this tree
        public:
            /** The node type */
            typedef Node<T, KeyType>                                                    NodeT;
            /** The node type */
            typedef NodeInfo<T, KeyType>                                                NodeInfoT;
            /** The key comparator */
            typedef Policy                                                              CompareT;
            /** The end iterator */
            typedef EndIterator<T, KeyType>                                             EndIterT;
            /** The Levenshtein iterator */
            typedef LevIterator<T, KeyType>                                             LevIterT;


            // Constants
        public:
            /** The tree size grow count */
            enum    { GrowStep = 1024 };

            // Members
        private:
            /** Node array */
            NodeT   *           array;
            /** The node count */
            uint32              count;
            /** The object count */
            uint32              objCount;
            /** The node allocated size */
            uint32              allocatedSize;
            /** Number of deleted keys */
            uint32              deletionCount;
            /** The root node index
                Will be zero in almost all tries, as the tree doesn't handle rebalancing yet */
            NodeIndex           rootIndex;

            // Construction and destruction
        public:
            /** The default constructor */
            Tree() : array(0), count(0), objCount(0), allocatedSize(0), deletionCount(0), rootIndex(badIndex) {}
            /** The destructor */
            ~Tree() { Free(); }

            // Helpers
        private:
            /** Grow the tree of the grow amount without discarding the previous data */
            inline bool growTree()
            {
                uint32 newAllocatedSize = allocatedSize + GrowStep;
                NodeT * newArray = (NodeT*)realloc(array, newAllocatedSize * sizeof(NodeT));
                if (newArray) { array = newArray; memset(&newArray[allocatedSize], -1, sizeof(NodeT) * GrowStep); for (uint32 i = 0; i < GrowStep; i++) newArray[allocatedSize+i].data = 0; allocatedSize = newAllocatedSize; return true; }
                // Not enough memory
                return false;
            }

            /** Find the key size, if it's zero terminated */
            inline uint32 findKeysSize(const KeyType * keys) const      { uint32 i = 0; while (Transform::simplifyKey(keys[i])) ++i; return i; }
            /** Compute the balance for the tree.
                This is used in insertion operation to get the fastest operation results while searching.
                @warning This is recursive and only used for debugging the tree */
            inline void computeBalanceAndHeight(NodeInfoT & currentNodeInfo) const
            {
                if (!currentNodeInfo.node) return;

                NodeIndex lower = currentNodeInfo.node->isEnd() ? badIndex : currentNodeInfo.node->lower;
                NodeIndex higher = currentNodeInfo.node->higher;

                NodeInfoT highBalance(higher, higher != badIndex ? &array[higher] : 0);
                NodeInfoT lowBalance(lower, lower != badIndex ? &array[lower] : 0);
                computeBalanceAndHeight(highBalance);
                computeBalanceAndHeight(lowBalance);

                currentNodeInfo.height = (lower != badIndex || higher != badIndex) ? max(lowBalance.height, highBalance.height) + 1 : 0;
                currentNodeInfo.balance = highBalance.height - lowBalance.height;
                currentNodeInfo.higherBalance = highBalance.balance;
                currentNodeInfo.lowerBalance = lowBalance.balance;
            }

            /** Balance a node */
            void balanceNode(NodeInfoT & balance)
            {
                // If unbalanced, need to recompute the balance first
                if(balance.height == -1) computeBalanceAndHeight(balance);
                balance.balanceDone = false;
                if(balance.balance > 1)
                {   // Need to balance the higher branch
                    balance.higherBalance > 0 ? hh(balance) : hl(balance);
                    balance.balanceDone = true;
                }
                else if(balance.balance < -1)
                {
                    balance.lowerBalance < 0 ? ll(balance) : lh(balance);
                    balance.balanceDone = true;
                }

                Assert(Abs(balance.balance) < 2);
                Assert(Abs(balance.lowerBalance) < 2);
                Assert(Abs(balance.higherBalance) < 2);
            }

            /** Rotate lower nodes */
            void ll(NodeInfoT & balance)
            {
                Assert(balance.node && !balance.node->isEnd()); // You should not call this method with a bad node
                NodeIndex lower = balance.node->lower;
                NodeT * lowerNode = &array[lower];
                NodeIndex prevHigher = lowerNode->higher;
                balance.node->lower = prevHigher;
                lowerNode->higher = balance.index;

                balance.index = lower;
                balance.node = lowerNode;
                balance.height = balance.height - 1;
                balance.balance = 0;
                balance.higherBalance = 0;
            }

            /** Rotate higher nodes */
            void hh(NodeInfoT & balance)
            {
                Assert(balance.node && balance.node->higher != badIndex); // You should not call this method with a bad node
                NodeIndex higher = balance.node->higher;
                NodeT * higherNode = &array[higher];
                Assert(!higherNode->isEnd());
                NodeIndex nextLower = higherNode->lower;
                balance.node->higher = nextLower;
                higherNode->lower = balance.index;

                balance.index = higher;
                balance.node = higherNode;
                balance.height = balance.height - 1;
                balance.balance = 0;
                balance.lowerBalance = 0;
            }
            /** Rotate lower with higher node */
            void lh(NodeInfoT & balance)
            {
                Assert(balance.node && !balance.node->isEnd()); // You should not call this method with a bad node
                NodeInfoT lower(balance.node->lower, &array[balance.node->lower]);
                hh(lower);
                balance.node->lower = lower.index;
                ll(balance);
            }
            /** Rotate higher with lower node */
            void hl(NodeInfoT & balance)
            {
                Assert(balance.node && balance.node->higher != badIndex); // You should not call this method with a bad node
                NodeInfoT higher(balance.node->higher, &array[balance.node->higher]);
                ll(higher);
                balance.node->higher = higher.index;
                hh(balance);
            }


            // Interface
        public:
            /** Free the tree and destruct the given object */
            inline void Free() {  if (array) { for (NodeIndex iter = rootIndex; iter < allocatedSize; iter++) { if (array[iter].isEnd()) DeleterT::deleter(array[iter].data, array[iter].splitChar); } free(array); array = 0; count = objCount = allocatedSize = 0; rootIndex = badIndex; } }
            /** Get the deletion count */
            inline uint32 getDeletionCount() const { return deletionCount; }
            
            /** Reassemble an optimized tree. This is likely very slow since it must hit all valid keys in the current tree to work.
                It also requires the same memory size as the current tree for its new value */
            bool reassemble()
            {
                if (!deletionCount) return true;
                Tree other;
                EndIterT iter(count, array, rootIndex);
                iter.goNextEndNode();
                
                KeyType * bufferKey = 0;
                uint32 maxKeyLength = 0;
                while (iter.isValid())
                {
                    uint32 keyLength = iter.computeKeyLength();
                    // Only realloc it required
                    if (keyLength > maxKeyLength)
                    {
                        bufferKey = (KeyType*)Platform::safeRealloc(bufferKey, keyLength * sizeof(KeyType));
                        if (!bufferKey) return false;
                        maxKeyLength = keyLength;
                    }
                    iter.getKeyName(bufferKey, keyLength);
                    if (!other.insertInTree(bufferKey, *iter, keyLength)) return false;
                    
                    ++iter;
                }
                // Ok, now swap pointers
                free(array);
                array = other.array;
                allocatedSize = other.allocatedSize;
                count = other.count;
                objCount = other.objCount;
                deletionCount = 0;
                
                other.array = 0;
                return true;
            }

            /** Traverse the tree and apply the visitor on it.
                @param instance         The instance of the function object to apply
                @param method           The method of the function object to call. This method mustn't throw and return 0 on error. Its signature must be int (const KeyType & key, T*, int, int).
                @param startLevel       The offset to add to the level number while calling the method
                @warning The method doesn't uses iterators (recursive method) so it can overflow stack for huge tree

                @code
                    // If you have
                    struct A { int DoSomeWork(Key key, T * data, int Level, int type); };
                    // You can call the findChild like this
                    A a;
                    rootNode->traverseTree(a, &A::DoSomeWork);
                @endcode

                @return true on success or false
            */
            template <typename Obj, typename Process>
            inline bool traverseTree(const Obj & instance, const Process & method, const uint32 startLevel = 0, NodeIndex startIndex = 0) const throw()
            {
                uint32 level = startLevel;
                NodeIndex current = startIndex;
                if (current >= count) return true;

                // Traverse the lower limit first
                if (!array[current].isEnd() && array[current].lower != badIndex && !traverseTree(instance, method, level+1, array[current].lower)) return false;
                // Traverse the equal ID
                if (!(instance.*method)(array[current].splitChar, array[current].isEnd() ? array[current].data : 0, level, current)) return false;
                if (!array[current].isEnd())
                {
                    if ((current+1) < count && !traverseTree(instance, method, level+1, current+1)) return false;
                }
                if (array[current].higher != badIndex && !traverseTree(instance, method, level+1, array[current].higher)) return false;

                return true;
            }

            /** Search the tree for a given key string.
                This search is exclusive so either the specified key is entirely in the tree,
                and the search will send back the associated data, either it is not in the tree,
                and the search will return 0.

                @param keys     The keys array to search for
                @param keySize  The keys array size (if not set, the array is supposed to be zero terminated)


                @note This method is iterative, so it is safe to call on very large trie
                @return The data for the key if found, or 0 if not found */
            inline T* searchFor(const KeyType * keys, const uint32 keySize = (uint32)-1) const
            {
                if (!keys) return 0;
                if (keySize == (uint32)-1) const_cast<uint32 &>(keySize) = findKeysSize(keys);
                NodeIndex current = rootIndex;
                uint32 keyIndex = 0;
                while (current < count && keyIndex < keySize)
                {
                    CompareT keyToCheck(array[current].splitChar);
                    typename CompareT::Result compareResult = keyToCheck.Compare(Transform::simplifyKey(keys[keyIndex]));
                    switch (compareResult)
                    {
                    case Comparator::NotDecided:
                    case Comparator::Equal:
                        keyIndex ++;
                        if (keyIndex == keySize)
                        {
                            if (array[current+1].isEnd()) return array[current+1].data;
                            if (array[current+1].lower != badIndex && array[array[current+1].lower].isEnd() && !array[array[current+1].lower].splitChar)
                                return array[array[current+1].lower].data;
                        }
                        current ++; // The same level node are just after
                        break;
                    case Comparator::Greater:
                        current = array[current].lower;
                        break;
                    case Comparator::Less:
                        current = array[current].higher;
                        break;
                    }
                }
                return 0;
            }

            /** Search the tree for a given key string with captures.
                This search is exclusive so either the specified key is entirely in the tree,
                and the search will send back the associated data, either it is not in the tree,
                and the search will return 0.
                Unlike searchFor, this search method should be used when using a ReservedComparable comparator.
                Any other method will ignore the reserved characters in the Trie, and works as if the keys were simple.

                With this method, if the Trie contains ('/a/b/#/d' and '/a/b/"/'), then when searched for
                "/a/b/-12345.342/d" will return the first object (the one with the hash), and the capture will mark
                positions '-' and '/'.

                Capturing is done by remembering start and end position in the given key.
                The method does not pre-allocate the capture array (it can be null) so it must be at least 2x maximum
                number of captures

                @param keys         The keys array to search for
                @param capt         The capture array pointer (must point either to null or to a 2x maximum possible capture count)
                @param captCount    If provided, will be filled to the number of captured elements
                @param keySize      The keys array size (if not set, the array is supposed to be zero terminated)


                @note This method is iterative, so it is safe to call on very large trie
                @return The data for the key if found, or 0 if not found */
            inline T* searchForWithCapture(const KeyType * keys, uint32 * capt = 0, uint32 * captCount = 0, const uint32 keySize = (uint32)-1) const
            {
                if (!keys) return 0;
                if (keySize == (uint32)-1) const_cast<uint32 &>(keySize) = findKeysSize(keys);
                NodeIndex current = rootIndex;
                uint32 keyIndex = 0, lastCapt = badIndex;
                if (captCount) *captCount = 0;
                while (current < count && keyIndex < keySize)
                {
                    CompareT keyToCheck(array[current].splitChar);
                    typename CompareT::Result compareResult = keyToCheck.Compare(Transform::simplifyKey(keys[keyIndex]));
                    switch (compareResult)
                    {
                    case Comparator::NotDecided:
                        if (capt && lastCapt == badIndex) lastCapt = keyIndex;
                        keyIndex ++;
                        if (array[current+1].isEnd())
                        {
                            if (keyIndex == keySize)
                            {
                                if (captCount && lastCapt != badIndex) (*captCount)++;
                                if (capt && lastCapt != badIndex) { *capt++ = lastCapt; *capt++ = keyIndex; lastCapt = badIndex; }
                                return array[current+1].data;
                            }
                            // Check if the next key is still undecided
                            if (keyIndex < keySize && keyToCheck.Compare(Transform::simplifyKey(keys[keyIndex])) != Comparator::NotDecided)
                            {
                                // It's not, so let's move to the next char
                                if (captCount) (*captCount)++;
                                if (capt) { *capt++ = lastCapt; *capt++ = keyIndex; lastCapt = badIndex; }
                                current = array[current+1].higher;
                            }
                        } else if (keyIndex < keySize && keyToCheck.Compare(Transform::simplifyKey(keys[keyIndex])) != Comparator::NotDecided)
                        {
                            // Next key is not undecided, so let's move to the next char to check if the rest is matching
                            if (captCount) (*captCount)++;
                            if (capt) { *capt++ = lastCapt; *capt++ = keyIndex; lastCapt = badIndex; }
                            // We might need to revert searching bit in case of conflicting character (for example, in '#.jpg', '#' globs '.', yet it's clearly not wanted) 
                            if (CompareT(array[current+1].splitChar).Compare(Transform::simplifyKey(keys[keyIndex-1])) == Comparator::Equal)
                                keyIndex--; 
                            current++;
                        }

                        break;
                    case Comparator::Equal:
                        keyIndex ++;
                        if (captCount && lastCapt != badIndex) (*captCount)++;
                        if (capt && lastCapt != badIndex) { *capt++ = lastCapt; *capt++ = keyIndex; lastCapt = badIndex; }
                        if (keyIndex == keySize)
                        {
                            if (array[current+1].isEnd()) return array[current+1].data; // No other value that works as prefix here
                            if (array[current+1].lower < count && array[array[current+1].lower].isEnd()) return array[array[current+1].lower].data; // For example, we have both 'a' and 'ab' in tree and searching for 'a'
                        }
                        current ++; // The same level node are just after
                        break;
                    case Comparator::Greater:
                        if (captCount && lastCapt != badIndex) (*captCount)++;
                        if (capt && lastCapt != badIndex) { *capt++ = lastCapt; *capt++ = keyIndex; lastCapt = badIndex; }
                        current = array[current].lower;
                        break;
                    case Comparator::Less:
                        if (captCount && lastCapt != badIndex) (*captCount)++;
                        if (capt && lastCapt != badIndex) { *capt++ = lastCapt; *capt++ = keyIndex; lastCapt = badIndex; }
                        current = array[current].higher;
                        break;
                    }
                }
                if (captCount) *captCount = 0;
                return 0;
            }

            /** Prefix search the tree for a given key string.
                This search will return an EndIterator for the first match with the given prefix.
                For example, this search will find "hamming" and "habit" if searched with prefix "ha"
                @param keys     The keys array to search for
                @param keySize  The keys array size (if not set, the array is supposed to be zero terminated)

                @note This method is iterative, so it is safe to call on very large trie
                @return The data for the key if found, or 0 if not found */
            inline EndIterT searchForPrefix(const KeyType * keys, const uint32 keySize = (uint32)-1) const
            {
                if (!keys) return EndIterT();
                if (keySize == (uint32)-1) const_cast<uint32 &>(keySize) = findKeysSize(keys);
                NodeIndex current = rootIndex;
                uint32 keyIndex = 0;
                while (current < count && keyIndex < keySize)
                {
                    CompareT keyToCheck(array[current].splitChar);
                    typename CompareT::Result compareResult = keyToCheck.Compare(Transform::simplifyKey(keys[keyIndex]));
                    switch (compareResult)
                    {
                    case Comparator::NotDecided:
                    case Comparator::Equal:
                        keyIndex ++;
                        if (keyIndex == keySize)
                        {
                            // Found the prefix, so build the iterator now
                            EndIterT iter(count, array, current);
                            iter.goNextEndNode();
                            return iter;
                        }
                        current ++; // The same level node are just after
                        break;
                    case Comparator::Greater:
                        current = array[current].lower;
                        break;
                    case Comparator::Less:
                        current = array[current].higher;
                        break;
                    }
                }
                return 0;
            }


            /** Search the tree with placeholder.
                This search will return an LevIterator for the first match with the given placeholder.
                The placeholder char what you specified (default to '.')
                For example, this search will find "jerry" and "gerry" if searched with key ".erry"
                @param keys             The keys array to search for
                @param keySize          The keys array size (if not set, the array is supposed to be zero terminated)
                @param placeHolder      The place holder char (default to '.')
                @param startSearchFrom  This should not be modified in user code (leave as-is)

                @warning This method is recursive, so it is not safe to call on very large trie (150k English words is a maximum for performance reasons)
                @return The data for the key if found, or 0 if not found */
            inline LevIterT searchWithPlaceholder(const KeyType * keys, const uint32 keySize = (uint32)-1, const KeyType placeHolder = (KeyType)'.', NodeIndex startSearchFrom = 0) const
            {
                // Check arguments
                if (!keys) return LevIterT(array);
                if (keySize == (uint32)-1) const_cast<uint32 &>(keySize) = findKeysSize(keys);

                // The returned iterator
                LevIterT iter(array);
                NodeIndex current = startSearchFrom;
                uint32 keyIndex = 0;
                while(current < count)
                {
                    const NodeT & n = array[current];
                    // If the node is an end node,
                    if (n.isEnd())
                    {
                        // Append all the string if we ended this key
                        if (keySize == keyIndex)
                        {   // Here we are done
                            iter.appendIndex(current);
                            break;
                        }
                        current = n.higher;
                        continue;
                    }

                    if (keyIndex == keySize)
                    {
                        // Check lower node, as we might have a end node in a lowest path (like searching for "mum" in [ "mummy", "mum"  while mummy was inserted first]
                        // Tree is like: m-u-m-m-y-* *    (key is "mum", we are on 2nd node 'm')
                        //                      \___/^
                        current = n.lower;
                        continue;
                    }

                    // Now the classical binary search
                    if (keys[keyIndex] == placeHolder)
                    {
                        // Follow less branch (actually, what after less branch)
                        if (n.lower < count)
                        {   // For example: m-u-m-m-y-* *  with mum.  (while testing '.')
                            //                   \_____/^     it should not follow the lower branch
                            if (!array[n.lower].isEnd())
                                // For example: m-u-m-m-y-* d-* with mu. (while testing '.')
                                //                   \_____/^ it should follow the lower branch
                                iter.appendIter(searchWithPlaceholder(&keys[keyIndex], keySize - keyIndex, placeHolder, n.lower));
                        }
                        // Follow current branch
                        iter.appendIter(searchWithPlaceholder(&keys[keyIndex + 1], keySize - keyIndex - 1, placeHolder, current+1));
                        // Follow greater branch
                        if (n.higher < count)
                        {   //                                        /\,
                            // Should follow only if we have f-u-z-z-*  y-*  searching for fuzz. (for example)
                            if (!array[n.higher].isEnd() )
                                iter.appendIter(searchWithPlaceholder(&keys[keyIndex], keySize - keyIndex, placeHolder, n.higher));
                        }
                        break;
                    }
                    CompareT keyToCheck(n.splitChar);
                    typename CompareT::Result compareResult = keyToCheck.Compare(Transform::simplifyKey(keys[keyIndex]));
                    switch (compareResult)
                    {
                    case Comparator::NotDecided:
                    case Comparator::Equal:
                        keyIndex ++;
                        current ++; // The same level node are just after
                        break;
                    case Comparator::Greater:
                        current = n.lower;
                        break;
                    case Comparator::Less:
                        current = n.higher;
                        break;
                    }
                }
                if (startSearchFrom == rootIndex) iter.Reset();
                return iter;
            }

            /** Levenshtein search the tree for a given key string.
                This search will return an LevIterator for the result pool.
                For example, this search will find "mum", "mummy" and "mom" if searched for "mum" and allowed errors (insertion/deletion/misuse) is 2.
                The result will be "mum", "mom" if searched for "mum" and allowed error is 1 (there is only one substitution from "mum" to "mom").
                "bud" and "bump" are accessible with a distance of 2.
                @param keys             The keys array to search for
                @param allowedDistance  The allowed number of error (0 for perfect match, 1 for fast search but limited fuzzy search, 2 for a good fuzzy search, more for low speed but general search)
                @param keySize          The keys array size (if not set, the array is supposed to be zero terminated)
                @param allowDeletion    Allow deletion (and insertion) in the matching process (so "Schmidt" and "Schmit" are separated by a distance of 1 not 2,
                                        and so is "Schmit" and "Schmmit"). When false, it's performing a Hamming search.
                @param startSearchFrom  The index to start the search from (user should never specify this)

                @warning This method is recursive, so it is not safe to call on very large trie (150k English words is a maximum for performance reasons)

                @return The data for the key if found, or 0 if not found */
            inline LevIterT searchForLevenshtein(const KeyType * keys, const uint32 allowedDistance = 2, const uint32 keySize = (uint32)-1, const bool allowDeletion = true, NodeIndex startSearchFrom = 0) const
            {
                // Check arguments
                if (!keys) return LevIterT(array);
                if (keySize == (uint32)-1) const_cast<uint32 &>(keySize) = findKeysSize(keys);

                // The returned iterator
                LevIterT iter(array);
                NodeIndex current = startSearchFrom;
                uint32 keyIndex = 0;
                while(current < count)
                {
                    const NodeT & n = array[current];
                    // If the node is an end node,
                    if (n.isEnd())
                    {
                        // Append all the remaining string that could afford the distance to the result array
                        if ((keySize - keyIndex) <= allowedDistance)
                        {
                            iter.appendIndex(current);
                        }
                        current = n.higher;
                        continue;
                    }

                    if (allowedDistance > 0)
                    {
                        if (n.lower < count)
                        {
                            iter.appendIter(searchForLevenshtein(&keys[keyIndex], allowedDistance, keySize - keyIndex, allowDeletion, n.lower));
                        }

                        if (keyIndex == keySize)
                        {   // Perform the search on lower nodes too
                            iter.appendIter(searchForLevenshtein(&keys[keyIndex], allowedDistance - 1, 0, allowDeletion, current+1));
                        } else
                        {   // Perform the search on mismatched results too
                            CompareT keyToCheck(n.splitChar);
                            typename CompareT::Result compareResult = keyToCheck.Compare(Transform::simplifyKey(keys[keyIndex]));

                            // Deletion
                            if (allowDeletion) iter.appendIter(searchForLevenshtein(&keys[keyIndex+1], allowedDistance - 1, keySize - keyIndex - 1, allowDeletion, current));
                            // Insertion
                            if (allowDeletion) iter.appendIter(searchForLevenshtein(&keys[keyIndex], allowedDistance - 1, keySize - keyIndex, allowDeletion, current+1));
                            // Mismatch for this position
                            iter.appendIter(searchForLevenshtein(&keys[keyIndex+1], allowedDistance - (compareResult != Comparator::Equal), keySize - keyIndex - 1, allowDeletion, current+1));
                        }
                        if (n.higher < count)
                            // Perform the search on higher node too
                            iter.appendIter(searchForLevenshtein(&keys[keyIndex], allowedDistance, keySize - keyIndex, allowDeletion, n.higher));

                        if (startSearchFrom == rootIndex) iter.Reset();
                        return iter;
                    }

                    if (keyIndex == keySize)
                    {
                        // Check lower node, as we might have a end node in a lowest path (like searching for "mum" in [ "mummy", "mum"  while mummy was inserted first]
                        current = n.lower;
                        continue;
                    }

                    // Now the classical binary search
                    CompareT keyToCheck(n.splitChar);
                    typename CompareT::Result compareResult = keyToCheck.Compare(Transform::simplifyKey(keys[keyIndex]));
                    switch (compareResult)
                    {
                    case Comparator::Equal:
                    case Comparator::NotDecided:
                        keyIndex ++;
                        current ++; // The same level node are just after
                        break;
                    case Comparator::Greater:
                        current = n.lower;
                        break;
                    case Comparator::Less:
                        current = n.higher;
                        break;
                    }
                }
                if (startSearchFrom == rootIndex) iter.Reset();
                return iter;
            }

            /** Remove a node from the tree.
                @param keys     The array of keys up to the data
                @param keySize  The array size in KeyType count
                @return true on success, false on memory error or key not found */
            inline bool deleteInTree(const KeyType * keys, const uint32 keySize = (uint32)-1)
            {
                if (!keys) return false;
                if (keySize == (uint32)-1) const_cast<uint32 &>(keySize) = findKeysSize(keys);
                
                uint32 keyIndex = 0;
                NodeIndex iter = rootIndex;
                NodeIndex * current = &iter;
                NodeIndex parent = badIndex;
                while (iter < count)
                {
                    CompareT keyToCheck(array[iter].splitChar);
                    typename CompareT::Result compareResult = keyToCheck.BasicCompare(Transform::simplifyKey(keys[keyIndex]));
                    if (compareResult == Comparator::Equal)
                    {
                        if (++keyIndex == keySize)
                        {   // A end node with that key already exist
                              if (!array[iter+1].isEnd()) return false; // Not found
                              // Ok, found, let's simply remove the data and not set it as end value anymore.
                              array[iter+1].state = NodeT::EmptyNode;
                              DeleterT::deleter(array[iter+1].data, array[iter+1].splitChar);
                              deletionCount++;
                              objCount--;
                              return true;
                        }
                        parent = iter;
                        iter++;  // Equal node are always just after the node
                        current = &iter;
                    } else if (compareResult == Comparator::Greater)
                    {
                        current = &array[iter].lower;
                        parent = iter;
                    }
                    else // Lower
                    {
                        current = &array[iter].higher;
                        parent = iter;
                    }
                    iter = *current;
                }
                return false;
            }

            /** Insert a node in the tree.
                @param keys     The array of keys up to the data
                @param data     A pointer on an allocated data which will be deleted with the deleter
                @param keySize  The array size in KeyType count
                @return true on success, false either if the key already exist or on memory error

                @note This method is iterative, so it is safe to call even on very large trie, but the data to insert must be sorted
                         first before inserting. */
            inline bool insertInTree(const KeyType * keys, T * data, const uint32 keySize = (uint32)-1)
            {
                // Check arguments
                if (!data || !keys) return false;
                if (keySize == (uint32)-1) const_cast<uint32 &>(keySize) = findKeysSize(keys);

                uint32 keyIndex = 0;
                NodeIndex iter = rootIndex;
                NodeIndex * current = &iter;
                NodeIndex parent = badIndex;
                while (iter < count)
                {
                    CompareT keyToCheck(array[iter].splitChar);
                    typename CompareT::Result compareResult = keyToCheck.BasicCompare(Transform::simplifyKey(keys[keyIndex]));
                    if (compareResult == Comparator::Equal)
                    {
                        if (++keyIndex == keySize)
                        {   // A end node with that key already exist
                            if (array[iter+1].isEnd()) return false;
                            else
                            {   // We have found the end of key, so need to prepare insert of end node in the next node's lower path
                                iter++;  // Equal node are always just after the node
                                current = &array[iter].lower;
                                parent = iter;
                                // Make sure the tree is big enough for the insertion
                                if ((count+2) > allocatedSize)
                                    // If the reallocation failed, just return
                                    if (!growTree()) return false; // Don't modify the lower node if allocation failed
                                iter = count;
                                *current = iter;  // Ok, modify it
                                array[iter].splitChar = 0;
                                array[iter].higher = badIndex;
                                array[iter].state = NodeT::EndNode;
                                array[iter].parent = parent;
                                array[iter].data = data;
                                count++;
                                if (rootIndex == badIndex) rootIndex = 0;
                                objCount++;
                                return true;
                            }
                        }
                        parent = iter;
                        iter++;  // Equal node are always just after the node
                        current = &iter;
                    } else if (compareResult == Comparator::Greater)
                    {
                        current = &array[iter].lower;
                        parent = iter;
                    }
                    else // Lower
                    {
                        current = &array[iter].higher;
                        parent = iter;
                    }
                    iter = *current;
                }

                // Make sure the tree is big enough for the insertion
                if ((count+2) > allocatedSize)
                    // If the reallocation failed, just return
                    if (!growTree()) return false;
                // Make sure the current iterator is on the last node
                iter = count;

                // From that point, there is no other node that match the suffix, so let's create them in that order
                *current = iter; // This line simply link the lower or higher index to the next node
                for (;;)
                {
                    array[iter].splitChar = Transform::simplifyKey(keys[keyIndex]);
                    array[iter].lower = array[iter].higher = badIndex;
                    array[iter].state = NodeT::PathNode;
                    array[iter].parent = parent;
                    count++;
                    if (++keyIndex == keySize)
                    {   // Finished the mapping, let's save the key in the next node
                        parent = iter;
                        iter ++;
                        array[iter].splitChar = 0; // End node has the minimum possible value
                        array[iter].higher = badIndex;
                        array[iter].state = NodeT::EndNode;
                        array[iter].parent = parent;
                        array[iter].data = data;
                        count++;
                        if (rootIndex == badIndex) rootIndex = 0;
                        objCount++;
                        return true;
                    }
                    parent = iter;
                    iter ++;
                }
            }

            /** Get the first iterator */
            inline EndIterT getFirstIterator() const
            {
                NodeIndex current = rootIndex;
                while (current < count)
                {
                    if (array[current].lower == badIndex) break;
                    current = array[current].lower;
                }
                EndIterT iter(count, array, current, true);
                if (current < count) iter.goNextEndNode();
                return iter;
            }

            /** Get the number of items saved in the tree */
            inline size_t getSize() const { return (size_t)objCount; }
        };

        // Include specialization for Unicode 16bit transform
        #include "template/unicodeTransform.hpp"
    }
}

#ifdef _MSC_VER
#pragma warning(pop)
#endif

#endif
