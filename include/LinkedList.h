#ifndef LINKED_LIST_H_INCLUDED
#define LINKED_LIST_H_INCLUDED

template <typename T>
class StackLinkedNode;

/* LinkedList is a very fast singly linked list.
 * It keeps only StackLinkedNode objects, allocated on stack.
 * Asymptotic complexity:
 *  insert: O(1)
 *  erase:
 *      all but the last: O(1)
 *      the last element: O(N)
 *  access:
 *      head: O(1)
 */
template <typename T>
class LinkedList
{
typedef StackLinkedNode<T> nodeType;
protected:
    nodeType* m_head;
public:
    LinkedList() : m_head(0) { }
    void insert(nodeType& node) {
        node.setNext(m_head);
        m_head = &node;
    }
    void erase(nodeType* node) {
        if (m_head == node) {
            //If the node is the first node
            // we replace the first node with the next one
            m_head = node->getNext();
            return;
        }
        if ( node->hasNext() ) {
            // If it is not the last element of the list
            //  we replace itself with the next one
            nodeType* nextNode = node->getNext();
            node->setData( nextNode->getData() )
                ->setNext( nextNode->getNext() );
        } else {
            // This is the last node, we have to find the previous
            // node in the list and fixup the link
            nodeType* previousNode = findPreviousOf(node);
            previousNode->setNext(0);
        }
    }
    nodeType* getHead() {
        return m_head;
    }
private:
    nodeType* findPreviousOf(nodeType* needle) {
        for(nodeType* previousNode = m_head; previousNode != 0; ) {
            if (previousNode->getNext() == needle)
                return previousNode;
            else
                previousNode = previousNode->getNext();
        }
        //something is really wrong;
        return 0;
    }
};

template <typename T>
class StackLinkedNode
{
typedef StackLinkedNode<T> selfType;
private:
    void* operator new(size_t);
    void operator delete(void*);
    StackLinkedNode(const selfType&);
protected:
    T* m_data;
    selfType* m_next;
public:
    StackLinkedNode() : m_data(0), m_next(0) {}
    selfType& operator=(const selfType& right) {
        m_data = right.m_data;
        m_next = this->m_next; // We copy only data
        return *this;
    }

    selfType* setNext(selfType* next) {
        m_next = next;
        return this;
    }
    selfType* setData(T* data) {
        m_data = data;
        return this;
    }
    selfType* getNext() {
        return m_next;
    }
    bool hasNext() {
        return m_next != 0;
    }
    T* getData() {
        return m_data;
    }
    T* getData() const {
        return m_data;
    }
};

#endif
