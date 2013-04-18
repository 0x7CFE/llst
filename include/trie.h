/*
 * Copyright (c) 2012, Ranjith TV
 * All rights reserved.
 *
 * Licensed under the BSD 3-Clause ("BSD New" or "BSD Simplified") license.
 * You may obtain a copy of the License at
 *
 * http://www.opensource.org/licenses/BSD-3-Clause
 *
 */

#ifndef TRIE_H
#define TRIE_H

#include <vector>
#include <set>
#include <algorithm>

namespace rtv
{

template < typename T,
typename V,
typename Cmp,
typename Items > class Node;

template < typename T,
typename V,
typename Cmp,
typename Items > class NodeItem
{
private:
    typedef Node<T, V, Cmp, Items> NodeClass;
    typedef NodeItem<T, V, Cmp, Items> NodeItemClass;

public:
    NodeItem(const T &endSymbol, T const &key)
            : mEndSymbol(endSymbol),
            mKey(key),
            mChilds(0) {}

    virtual ~NodeItem() {
        delete mChilds;
    }

    bool operator<(NodeItemClass const &oth) const {
        return Cmp()(this->mKey, oth.mKey);
    }

    bool operator<(T const &key) const {
        return Cmp()(this->mKey, key);
    }

    bool operator==(T const &key) const {
        return !Cmp()(this->mKey, key) && !Cmp()(key, this->mKey);
    }

    bool operator==(NodeItemClass const &oth) const {
        return !Cmp()(this->mKey, oth.mKey) && !Cmp()(oth.mKey, this->mKey);
    }

    bool operator!=(T const &key) const {
        return !((*this) == key);
    }

    bool operator!=(NodeItemClass const &oth) const {
        return !((*this) == oth);
    }

    void set(T const &key) {
        mKey = key;
    }

    const T &get() const {
        return mKey;
    }

    const NodeClass *getChilds() const {
        return mChilds;
    }

    NodeClass *getChilds() {
        return mChilds;
    }

    NodeClass *getOrCreateChilds(NodeClass * parent) {
        createChilds(parent);
        return mChilds;
    }

private:
    void createChilds(NodeClass * parent) {
        if (!mChilds) {
            mChilds = new NodeClass(mEndSymbol, parent);
        }
    }

    NodeItem(NodeItem const &);
    NodeItem &operator=(NodeItem const &);

private:
    const T mEndSymbol;
    T mKey;
    NodeClass *mChilds;
};

template < typename T,
typename V,
typename Cmp,
typename Items > class EndNodeItem: public NodeItem<T, V, Cmp, Items>
{
private:
    typedef NodeItem<T, V, Cmp, Items> ParentClass;

public:
    EndNodeItem(const T &endSymbol, T const &key)
            : ParentClass(endSymbol, key) {}

    EndNodeItem(const T &endSymbol, T const &key, V const &value)
            : ParentClass(endSymbol, key),
            mValue(value) {}

    void set(T const &key, V const &value) {
        ParentClass::set(key);
        mValue = value;
    }

    const V &getValue() const {
        return mValue;
    }

    V &getValue() {
        return mValue;
    }

private:
    EndNodeItem(EndNodeItem const &);
    EndNodeItem &operator=(EndNodeItem const &);

private:
    V mValue;
};

template < typename T,
typename V,
typename Cmp,
typename Items > class NodeItemPtrCompare
{
private:
    typedef NodeItem<T, V, Cmp, Items> NodeItemClass;

public:
    bool operator()(const NodeItemClass *v1, const NodeItemClass *v2) {
        return *v1 < *v2;
    }
};

template < typename T,
typename V,
typename Cmp,
typename Items > class Node
{
public:
    typedef NodeItem<T, V, Cmp, Items> NodeItemClass;
    typedef EndNodeItem<T, V, Cmp, Items> EndNodeItemClass;
    typedef Node<T, V, Cmp, Items> NodeClass;

public:

    class ConstIterator
    {
    protected:
        typedef std::pair<const T *, const V *> KeyValuePair;
        typedef typename NodeClass::ItemsContainerConstIter ItemsContainerConstIter;

    public:
        ConstIterator(const NodeClass *node, const NodeClass * root, const T * key = 0, bool mooveToEnd = false)
                : mRootNode(root),
                  mCurrentNode(node),
                  mCheckKeyLeft(false),
                  mCheckKeyRight(true),
                  mEndReached(false) {
            if (!root) {
                mRootNode = node;
            }
            pushNode(node, key, mooveToEnd);
            if (!mooveToEnd) {
                next();
            }
        }

        ConstIterator(const ConstIterator & oth)
                : mRootNode(oth.mRootNode),
                mCurrentNode(oth.mCurrentNode),
                mCurrentPos(oth.mCurrentPos),
                mKeyStack(oth.mKeyStack),
                mCheckKeyLeft(oth.mCheckKeyLeft),
                mCheckKeyRight(oth.mCheckKeyRight),
                mEndReached(oth.mEndReached) {
            if (!mKeyStack.empty()) {
                mKeyValuePair = std::make_pair(&mKeyStack[0], oth.mKeyValuePair.second);
            }
        }

        ConstIterator & operator=(const ConstIterator & oth) {
            if (this != &oth) {
                mRootNode = oth.mRootNode;
                mCurrentNode = oth.mCurrentNode;
                mCurrentPos = oth.mCurrentPos;
                mKeyStack = oth.mKeyStack;
                if (!mKeyStack.empty()) {
                    mKeyValuePair = std::make_pair(&mKeyStack[0], oth.mKeyValuePair.second);
                }
                mCheckKeyLeft = oth.mCheckKeyLeft;
                mCheckKeyRight = oth.mCheckKeyRight;
                mEndReached = oth.mEndReached;
            }
            return *this;
        }

        const KeyValuePair &operator*() const {
            return this->mKeyValuePair;
        }

        const KeyValuePair *operator->() const {
            return &this->mKeyValuePair;
        }

        bool operator==(ConstIterator const &oth) const {
            return this->equals(oth);
        }

        bool operator!=(ConstIterator const &oth) const {
            return !(*this == oth);
        }

        ConstIterator operator++(int) {
            ConstIterator iter = *this;
            ++(*this);
            return iter;
        }

        ConstIterator &operator++() {
            this->next();
            return *this;
        }

        ConstIterator operator--(int) {
            ConstIterator iter = *this;
            --(*this);
            return iter;
        }

        ConstIterator &operator--() {
            this->previous();
            return *this;
        }

    protected:
        friend class Node<T, V, Cmp, Items>;

        const NodeClass * mRootNode;
        const NodeClass * mCurrentNode;
        ItemsContainerConstIter mCurrentPos;
        std::vector<T> mKeyStack;
        KeyValuePair mKeyValuePair;
        bool mCheckKeyLeft;
        bool mCheckKeyRight;
        bool mEndReached;

    protected:
        void previous() {
            if (!mCurrentNode->mItems.empty() && mCurrentPos == mCurrentNode->mItems.end()) {
                --mCurrentPos;
            }

            bool newNode = false;
            bool oldNode = false;

            while (mEndReached || !isLeftEnd()) {
                mEndReached = false;
                ItemsContainerConstIter iterBegin = mCurrentNode->mItems.begin();
                if (!mKeyStack.empty()) {
                    if (mKeyStack.back() == mCurrentNode->endSymbol()) {
                        mKeyStack.pop_back();
                        mCurrentPos = mCurrentNode->mItems.begin();
                        if (isLeftEnd()) {
                            break;
                        }
                    }
                }

                if (!newNode && !mKeyStack.empty()) {
                    if (mCheckKeyLeft) {
                        mCurrentNode = mCurrentNode->parent();
                        mCurrentPos = mCurrentNode->mItems.find(mKeyStack.back());
                        iterBegin = mCurrentNode->mItems.begin();
                        mKeyStack.pop_back();
                        mCheckKeyLeft = false;
                        if (mCurrentPos != iterBegin) {
                            --mCurrentPos;
                            oldNode = false;
                        } else {
                            oldNode = true;
                        }
                    } else {
                        ItemsContainerConstIter iter = mCurrentNode->mItems.find(mCurrentNode->endSymbol());
                        if (iter != mCurrentNode->mItems.end()) {
                            mCurrentPos = iter;
                            const NodeItemClass & item = *(const NodeItemClass *) * mCurrentPos;
                            mKeyStack.push_back(item.get());
                            mKeyValuePair.first = &(mKeyStack[0]);
                            mKeyValuePair.second = &(((const EndNodeItemClass &)item).getValue());
                            mCheckKeyLeft = true;
                            return;
                        } else {
                            mCurrentNode = mCurrentNode->parent();
                            mCurrentPos = mCurrentNode->mItems.find(mKeyStack.back());
                            iterBegin = mCurrentNode->mItems.begin();
                            mKeyStack.pop_back();
                            mCheckKeyLeft = false;
                            if (mCurrentPos != iterBegin) {
                                --mCurrentPos;
                                oldNode = false;
                            } else {
                                oldNode = true;
                            }
                        }
                    }
                }

                newNode = false;
                for (; mCurrentPos != iterBegin; --mCurrentPos) {
                    if (*mCurrentPos) {
                        const NodeItemClass &item = *(const NodeItemClass *) * mCurrentPos;
                        if (item != mCurrentNode->endSymbol()) {
                            mKeyStack.push_back(item.get());
                            pushNode(item.getChilds(), 0, true);
                            --mCurrentPos;
                            newNode = true;
                            break;
                        }
                    }
                    oldNode = false;
                }
                if (!newNode && !oldNode) {
					if (mCurrentPos != mCurrentNode->mItems.end()) {
						if (*mCurrentPos) {
					        const NodeItemClass &item = *(const NodeItemClass *) * mCurrentPos;
				            if (item != mCurrentNode->endSymbol()) {
			                    mKeyStack.push_back(item.get());
		                        pushNode(item.getChilds(), 0, true);
	                            --mCurrentPos;
								newNode = true;
							}
						}
					}
				}

            }
            mCurrentPos = mCurrentNode->mItems.end();
            mEndReached = true;
        }

        void next() {
            while (!isEnd()) {
                ItemsContainerConstIter iterEnd = mCurrentNode->mItems.end();

                if (!mKeyStack.empty()) {
                    if (mKeyStack.back() == mCurrentNode->endSymbol()) {
                        mKeyStack.pop_back();
                        mCurrentPos = mCurrentNode->mItems.begin();
                    }
                }

                if (mCurrentPos == iterEnd && !mKeyStack.empty()) {
                    mCurrentNode = mCurrentNode->parent();
                    mCurrentPos = mCurrentNode->mItems.find(mKeyStack.back());
                    ++mCurrentPos;
                    iterEnd = mCurrentNode->mItems.end();
                    mKeyStack.pop_back();
                }

                for (; mCurrentPos != iterEnd; ++mCurrentPos) {
                    if (mCheckKeyRight) {
                        mCheckKeyRight = false;
                        ItemsContainerConstIter iter = mCurrentNode->mItems.find(mCurrentNode->endSymbol());
                        if (iter != iterEnd) {
                            mCurrentPos = iter;
                            const NodeItemClass & item = *(const NodeItemClass *) * mCurrentPos;
                            mKeyStack.push_back(item.get());
                            mKeyValuePair.first = &(mKeyStack[0]);
                            mKeyValuePair.second = &(((const EndNodeItemClass &)item).getValue());
                            mCheckKeyLeft = true;
                            return;
                        }
                    }

                    if (*mCurrentPos) {
                        const NodeItemClass &item = *(const NodeItemClass *) * mCurrentPos;
                        if (item != mCurrentNode->endSymbol()) {
                            mKeyStack.push_back(item.get());
                            pushNode(item.getChilds());
                            break;
                        }
                    }
                }
            }
            mEndReached = true;
        }

        bool isEnd() const {
            if (this->mRootNode == this->mCurrentNode &&
                this->mCurrentPos == this->mCurrentNode->mItems.end()) {
                return true;
            }
            return false;
        }

        bool isLeftEnd() const {
            if (this->mRootNode == this->mCurrentNode &&
                this->mCurrentPos == this->mCurrentNode->mItems.begin()) {
                return true;
            }
            return false;
        }

        bool equals(const ConstIterator & oth) const {
            if (this->isEnd() && oth.isEnd()) {
                return true;
            }

            if (this->mCurrentNode == oth.mCurrentNode &&
                    this->mCurrentPos  == oth.mCurrentPos) {
                return true;
            }

            return false;
        }

        void pushNode(const NodeClass *node, const T * key = 0, bool mooveToEnd = false) {
            mCurrentNode = node;
            mCheckKeyLeft = false;
            if (mooveToEnd) {
                mCurrentPos = mCurrentNode->mItems.end();
                mEndReached = true;
                mCheckKeyRight = false;
            } else {
                if (key) {
                    for (int i = 0; key[i] != node->endSymbol(); ++i) {
                        mKeyStack.push_back(key[i]);
                    }
                }
                mCurrentPos = node->mItems.begin();
                mCheckKeyRight = true;
            }
        }
    };

    class Iterator : public ConstIterator
    {
    private:
        typedef std::pair<const T *, V *> MutableKeyValuePair;
        MutableKeyValuePair mMutableKeyValuePair;

    private:
        MutableKeyValuePair & getPair() {
            mMutableKeyValuePair.first = this->mKeyValuePair.first;
            mMutableKeyValuePair.second = const_cast<V *>(this->mKeyValuePair.second);
            return this->mMutableKeyValuePair;
        }

    public:
        Iterator(NodeClass *node, NodeClass *root, const T * key = 0, bool mooveToEnd = false)
                : ConstIterator(node, root, key, mooveToEnd) {}

        MutableKeyValuePair &operator*() {
            return getPair();
        }

        MutableKeyValuePair *operator->() {
            return &(getPair());
        }

        Iterator operator++(int) {
            Iterator iter = *this;
            ++(*this);
            return iter;
        }

        Iterator &operator++() {
            this->next();
            return *this;
        }

        Iterator operator--(int) {
            Iterator iter = *this;
            --(*this);
            return iter;
        }

        Iterator &operator--() {
            this->previous();
            return *this;
        }
    };

private:
    Node(Node const &);
    Node &operator=(Node const &);

    static void deleteItem(NodeItemClass *item) {
        delete item;
    }

        
    NodeClass * nodeWithKey(const T *key) {
        return const_cast<NodeClass *>(const_cast<const NodeClass *>(this)->nodeWithKey(key));
    }

    const NodeClass * nodeWithKey(const T *key) const {
        const NodeClass * node = nodeWithPrefix(key);
        if (node) {
            if (node->mItems.getItem(mEndSymbol)) {
                return node;
            }
        }
        return 0;
    }

    const NodeClass * nodeWithPrefix(const T *prefix) const {
        int i=0;
        const NodeClass * node = this;

        while (node) {
            if (prefix[i] == mEndSymbol) {
                return node;
            }
            const NodeItemClass *item = node->mItems.getItem(prefix[i]);
            if (!item) {
                break;
            }

            node = item->getChilds();
            ++i;
        }
        return 0;
    }

    bool erase(NodeClass * node, const T * key) {
        bool erased = false;
        int keyIndex = 0;

        if (node && key) {
            bool finished = false;
            int count = 0;
            erased = true;

            if (!keyIndex) {
                while (key[keyIndex] != node->endSymbol()) {
                    ++ keyIndex;
                }
            }

            while (node && !finished && erased) {
                ItemsContainerIter iterEnd = node->mItems.end();

                count = 0;
                for (ItemsContainerIter iter = node->mItems.begin(); iter != iterEnd; ++iter) {
                    if (*iter != 0) {
                        ++count;
                    }
                }

                if (count > 1) {
                    erased = node->mItems.eraseItem(key[keyIndex]);
                    finished = true;
                } else if (count == 1) {
                    erased = node->mItems.eraseItem(key[keyIndex]);
                }

                --keyIndex;
                node = node->parent();
            }

        }
        if (erased) {
            --mSize;
        }
        return erased;
    }

public:
    Node(const T &eSymbol, NodeClass * parent = 0)
            : mItems(eSymbol),
            mEndSymbol(eSymbol),
            mSize(0),
            mParent(parent) {}

    ~Node() {
        clear();
    }

    T endSymbol() const {
        return mEndSymbol;
    }

    void clear() {
        std::for_each(mItems.begin(), mItems.end(), deleteItem);
        mItems.clear();
        mSize = 0;
    }

    bool empty() const {
        return size() == 0;
    }

    unsigned int size() const {
        return mSize;
    }

    std::pair<Iterator, bool> insert(const T *key, V const &value) {
        std::pair<Iterator, bool> result(end(), false);
        int i = 0;
        NodeClass * node = this;

        while (true) {
            std::pair<typename Items::Item *, bool> itemPair = node->mItems.insertItem(key[i]);
            NodeItemClass *item = itemPair.first;
            if (itemPair.second) {
                result.first = Iterator(node, this, key);
                break;
            }
            if (!item) {
                break;
            } else if (key[i] == mEndSymbol) {
                ((EndNodeItemClass *)item)->set(key[i], value);
                result.first = Iterator(node, this, key);
                result.second = true;
                ++mSize;
                break;
            } else {
                node = item->getOrCreateChilds(node);
            }
            ++i;
        }

        return result;
    }

    bool erase(Iterator pos) {
        if (pos.mCurrentNode && pos.mCurrentPos != pos.mCurrentNode->mItems.end()) {
            return erase(const_cast<NodeClass *>(pos.mCurrentNode), pos->first);
        }
        return false;
    }

    bool erase(const T *key) {
        NodeClass * node = nodeWithKey(key);
        if (node) {
            return erase(node, key);
        }
        return false;
    }

    const V *get(const T *key) const {
        return const_cast<NodeClass *>(this)->get(key);
    }

    V *get(const T *key) {
        NodeClass * node = nodeWithKey(key);
        if (node) {
            NodeItemClass *item = node->mItems.getItem(mEndSymbol);
            return &(((EndNodeItemClass *)item)->getValue());
        }
        return 0;
    }


    bool hasKey(const T *key) const {
        return get(key) != (V *)0;
    }

    const NodeClass * parent() const {
        return mParent;
    }

    NodeClass * parent() {
        return mParent;
    }

    ConstIterator begin() const {
        return ConstIterator(this, this);
    }

    ConstIterator end() const {
        return ConstIterator(this, this, 0, true);
    }

    Iterator begin() {
        return Iterator(this, this);
    }

    Iterator end() {
        return Iterator(this, this, 0, true);
    }

    ConstIterator find(const T *key) const {
        NodeClass * node = const_cast<NodeClass *>(this)->nodeWithKey(key);
        if (!node) {
            return ConstIterator(this, this, 0, true);
        } else {
            return ConstIterator(node, this, key);
        }
    }

    Iterator find(const T *key) {
        NodeClass * node = this->nodeWithKey(key);
        if (!node) {
            return Iterator(this, this, 0, true);
        } else {
            return Iterator(node, this, key);
        }
    }

    Iterator startsWith(const T *prefix) {
        const NodeClass * node = const_cast<const NodeClass *>(this)->nodeWithPrefix(prefix);
        if (!node) {
            return Iterator(this, this, 0, true);
        } else {
            return Iterator(const_cast<NodeClass *>(node), const_cast<NodeClass *>(node), prefix);
        }
    }

    ConstIterator startsWith(const T *prefix) const {
        const NodeClass * node = nodeWithPrefix(prefix);
        if (!node) {
            return ConstIterator(this, this, 0, true);
        } else {
            return ConstIterator(node, node, prefix);
        }
    }

private:
    typedef typename Items::iterator ItemsContainerIter;
    typedef typename Items::const_iterator ItemsContainerConstIter;

    Items mItems;
    const T mEndSymbol;
    unsigned int mSize;
    NodeClass * mParent;
};

/*!
 *
 */
template <typename T> class SymbolToIndexMapper
{
public:
    unsigned int operator()(const T & c) const {
        return static_cast<unsigned int>(c);
    }
};

/*!
 * @brief Container representing each node in the Trie.
 *
 *
 * With this class the container used for storing node item is STL vector.
 * Here each node will use a space propotional to Max.
 * For searching only constant time taken at each node.
 * @tparam T Type for each element in the key
 * @tparam V Type of the value that the key will be representing
 * @tparam Cmp Comparison functor
 * @tparam Max Maximum element that a Trie node can have
 */
template < typename T,
typename V,
typename Cmp,
int Max = 256,
typename M = SymbolToIndexMapper<T> > class VectorItems
{
public:
    typedef NodeItem<T, V, Cmp, VectorItems<T, V, Cmp, Max, M> > Item;
    typedef std::vector<Item *> Items;
    typedef typename Items::iterator iterator;
    typedef typename Items::const_iterator const_iterator;
    typedef Node<T, V, Cmp, VectorItems<T, V, Cmp, Max, M> > NodeClass;
    typedef typename NodeClass::NodeItemClass NodeItemClass;
    typedef typename NodeClass::EndNodeItemClass EndNodeItemClass;

public:
    VectorItems(T const &endSymbol)
            : mEndSymbol(endSymbol),
            mItems(Max, (Item *)0) {}

    const_iterator find(const T & k) const {
        const Item * item = getItem(k);
        if (item) {
            return mItems.begin() + mSymolToIndex(k);
        }
        return mItems.end();
    }

    iterator find(const T & k) {
        const Item * item = getItem(k);
        if (item) {
            return mItems.begin() + mSymolToIndex(k);
        }
        return mItems.end();
    }

    iterator begin() {
        return mItems.begin();
    }

    const_iterator begin() const {
        return mItems.begin();
    }

    iterator end() {
        return mItems.end();
    }

    const_iterator end() const {
        return mItems.end();
    }

    void clear() {
        std::fill(mItems.begin(), mItems.end(), (Item *)0);
    }

    bool empty() const {
        return mItems.empty();
    }

    std::pair<Item *, bool> insertItem(T const &k) {
        std::pair<Item *, bool> ret((Item *)0, false);
        if (!getItem(k)) {
            assignItem(k, createNodeItem(k));
            ret.first = getItem(k);
        } else {
            ret.first = getItem(k);
            if (k == mEndSymbol) {
                ret.second = true;
            }
        }
        return ret;
    }

    bool eraseItem(T const &k) {
        Item * item = getItem(k);
        if (item) {
            delete item;
            assignItem(k, (Item *)0);
            return true;
        } else {
            return false;
        }
    }

    Item *getItem(T const &k) {
        return mItems[mSymolToIndex(k)];
    }

    const Item *getItem(T const &k) const {
        return mItems[mSymolToIndex(k)];
    }

    void assignItem(T k, Item *i) {
        mItems[mSymolToIndex(k)] = i;
    }

    NodeItemClass *createNodeItem(T const &k) {
        if (k == mEndSymbol) {
            return new EndNodeItemClass(mEndSymbol, k);
        } else {
            return new NodeItemClass(mEndSymbol, k);
        }
    }

protected:
    const T mEndSymbol;
    Items mItems;
    M mSymolToIndex;
};

/*!
 * @brief Container representing each node in the Trie.
 *
 *
 * With this class the container used for storing node item is STL set.
 * Here no extra space will used for storing node item.
 * For searching in each node the time taken is propotional to number of item in the node.
 * @tparam T Type for each element in the key
 * @tparam V Type of the value that the key will be representing
 * @tparam Cmp Comparison functor
 */
template < typename T,
typename V,
typename Cmp > class SetItems
{
public:
    typedef NodeItem<T, V, Cmp, SetItems<T, V, Cmp> > Item;
    typedef std::set<Item *, NodeItemPtrCompare<T, V, Cmp, SetItems<T, V, Cmp> > > Items;
    typedef typename Items::iterator iterator;
    typedef typename Items::const_iterator const_iterator;
    typedef Node<T, V, Cmp, SetItems<T, V, Cmp> > NodeClass;
    typedef typename NodeClass::NodeItemClass NodeItemClass;
    typedef typename NodeClass::EndNodeItemClass EndNodeItemClass;

public:
    SetItems(T const &endSymbol)
            : mEndSymbol(endSymbol) {}

    const_iterator find(const T & k) const {
        return const_cast<SetItems<T, V, Cmp> *>(this)->find(k);
    }

    iterator find(const T & k) {
        Item tmp(mEndSymbol, k);
        return mItems.find(&tmp);
    }

    iterator begin() {
        return mItems.begin();
    }

    const_iterator begin() const {
        return mItems.begin();
    }

    iterator end() {
        return mItems.end();
    }

    const_iterator end() const {
        return mItems.end();
    }

    bool empty() const {
        return mItems.empty();
    }

    void clear() {
        mItems.clear();
    }

    std::pair<Item *, bool> insertItem(T const &k) {
        std::pair<Item *, bool> ret((Item*)0, false);
        Item tmp(mEndSymbol, k);
        iterator iter = mItems.find(&tmp);
        if (iter == mItems.end()) {
            Item *v = createNodeItem(k);
            mItems.insert(v);
            ret.first = v;
        } else {
            ret.first = (Item *) * iter;
            if (k == mEndSymbol) {
                ret.second = true;
            }
        }
        return ret;
    }

    bool eraseItem(T const &k) {
        Item tmp(mEndSymbol, k);
        iterator iter = mItems.find(&tmp);
        if (iter != mItems.end()) {
            delete *iter;
            mItems.erase(iter);
            return true;
        } else {
            return false;
        }
    }

    const Item *getItem(T const &k) const {
        return const_cast<SetItems *>(this)->getItem(k);
    }

    Item *getItem(T const &k) {
        Item tmp(mEndSymbol, k);

        iterator iter = mItems.find(&tmp);
        if (iter == mItems.end()) {
            return 0;
        }
        return (Item *)(*iter);
    }

    NodeItemClass *createNodeItem(T const &k) {
        if (k == mEndSymbol) {
            return new EndNodeItemClass(mEndSymbol, k);
        } else {
            return new NodeItemClass(mEndSymbol, k);
        }
    }

protected:
    const T mEndSymbol;
    Items mItems;
};

/*!
 * @mainpage Simple Trie
 * @section intro_sec Introduction
 * This is an implementation of prefix Trie data structure. This implementation is in C++ and using template.
 * @section features Features
 * Following are the main operations provided by the Trie.
 * <ul>
 * <li>Adding key, value pair
 * <li>Removing an element by key
 * <li>Checking the existence of a key
 * <li>Retrieving value by key
 * <li>Find elements with common prefix
 * <li>Iterator
 * </ul>
 */


/*!
 * @brief Trie main class
 * @tparam T Type for each element in the key
 * @tparam V Type of the value that the key will be representing
 * @tparam Cmp Comparison functor
 * @tparam Items The data structure that represents each node in the Trie.
 *               Items can be rtv::SetItems<T, V, Cmp> or rtv::VectorItems<T, V, Cmp, Max>,
 *               Max is the integer representing number of elements in each Trie node.
 *
 * @section usage_sec Usage of the Trie
 * @subsection usage_declaration Declarating the Trie
 * A Trie with key as chars and value as std::string can be declared as given below
 * @code
 * #include <string>
 * #include <trie.h>
 *
 * int main(int argc, char ** argv) {
 *
 *     rtv::Trie<char, std::string> dictionary('$');
 *
 *     return 0;
 * }
 * @endcode
 *
 * @subsection usage_population Populatiion and deletion from the Trie
 * Trie can be populated by using the Trie::insert method and element can be removed using Trie::erase.
 * @code
 * #include <trie.h>
 * #include <string>
 * #include <iostream>
 *
 * int main(int argc, char ** argv) {
 *
 *     rtv::Trie<char, std::string> dictionary('$');
 *
 *     // adding key value pair to the Trie
 *     if (dictionary.insert("karma$", "Destiny or fate, following as effect from cause").second) {
 *         std::cout << "added karma" << std::endl;
 *     }
 *
 *     // removing key from the Trie
 *     if (dictionary.erase("karma$")) {
 *         std::cout << "removed karma" << std::endl;
 *     }
 *
 *     return 0;
 * }
 *
 * @endcode
 * @subsection usage_retrieval Retrieval of Value
 * Value for a key can be retrieved using Trie::get method and
 * the existence of the key can be tested using Trie::hasKey method.
 * @code
 * #include <trie.h>
 * #include <string>
 * #include <iostream>
 *
 * int main(int argc, char ** argv) {
 *
 *     rtv::Trie<char, std::string> dictionary('$');
 *
 *     dictionary.insert("karma$", "Destiny or fate, following as effect from cause");
 *
 *     if (dictionary.hasKey("karma$")) {
 *         std::cout << "key karma found" << std::endl;
 *     }
 *     std::string * result = dictionary.get("karma$");
 *     if (result) {
 *         std::cout << result->c_str() << std::endl;
 *     }
 *
 *     return 0;
 * }
 * @endcode
 *
 * @subsection usage_searching Searching keys which have common prefix
 * Keys which begins with a specific charactars can be retrieved using Trie::startsWith method
 * @code
 * #include <trie.h>
 * #include <string>
 * #include <iostream>
 * #include <vector>
 *
 * int main(int argc, char ** argv) {
 *
 *     rtv::Trie<char, std::string> dictionary('\0');
 *
 *     dictionary.insert("karma", "Destiny or fate, following as effect from cause");
 *     rtv::Trie<char, std::string>::Iterator iter = dictionary.startsWith("kar");
 *
 *     for (; iter != dictionary.end(); ++iter) {
 *         std::cout << iter->first << " : " << iter->second->c_str() << std::endl;
 *     }
 *
 *     return 0;
 * }
 * @endcode
 *
 * @subsection usage_array_of_node Trie with each Node as an array
 * Here each node of the Trie is an array. The advantage is that the searching of a symbol in the array takes O(1) time (is constant time).
 * The disadvantage is that the array will have empty elements so the space used will more than actually required.
 *
 * @code
 *
 * #include <trie.h>
 * #include <string>
 * #include <iostream>
 * #include <vector>
 *
 * int main(int argc, char ** argv) {
 *
 *     // Here 256 is the size of array in each node
 *     rtv::Trie<char, std::string, std::less<char>,
 *               rtv::VectorItems<char, std::string, std::less<char>, 256> > dictionary('$');
 *
 *     return 0;
 * }
 * @endcode
 *
 * @subsection usage_vector_item Efficient use of Trie for alphabets
 * Below example shows how to use Trie to operate on alphabets efficiently.
 * Here VectorItems is used to store alphabets with size of 28 (26 alphabets + space + end symbol).
 *
 * @code
 * #include <trie.h>
 * #include <string>
 * #include <iostream>
 * #include <vector>
 * #include <cctype>
 *
 * class TrieCaseInsensitiveCompare
 * {
 * public:
 *     bool operator()(char v1, char v2) {
 *         int i1 = std::tolower(v1);
 *         int i2 = std::tolower(v2);
 *         return i1 < i2;
 *     }
 * };
 *
 * // key to vector index converter
 * // case insensitive and includes alphabets, space and end symbol
 * class AlphaToIndex
 * {
 * public:
 *     unsigned int operator()(const char & c) const {
 *         unsigned int index = 0;
 *         if (c == ' ') {
 *             index = 27;
 *         } else if (c >= 'A' && c <= 'Z') {
 *             index = c - 'A' + 1;
 *         } else if (c >= 'a' && c <= 'z') {
 *             index = c - 'a' + 1;
 *         }
 *         return index;
 *     }
 * };
 *
 * int main(int argc, char ** argv) {
 *
 *     rtv::Trie<char, std::string,
 *               TrieCaseInsensitiveCompare,
 *               rtv::VectorItems<char, std::string, TrieCaseInsensitiveCompare, 28, AlphaToIndex>
 *               > dictionary('\0');
 *
 *     // adding key value pair to the Trie
 *     if (dictionary.insert("karma", "Destiny or fate, following as effect from cause").second) {
 *         std::cout << "added karma" << std::endl;
 *     }
 *
 *     // removing key from the Trie
 *     if (dictionary.erase("karma")) {
 *         std::cout << "removed karma" << std::endl;
 *     }
 *
 *     return 0;
 * }
 *
 * @endcode
 *
 * @subsection usage_set_of_node Trie with each Node as a set
 * Here each node will be an ordered set.
 * Here there will be no extra usage of space but search for a symbol in the node takes logarithmic time.
 * Trie with this feature can also be used for caseinsensitive symbol handling.
 * @code
 *
 * #include <trie.h>
 * #include <string>
 * #include <iostream>
 * #include <set>
 *
 * int main(int argc, char ** argv) {
 *
 *     rtv::Trie<char, std::string, std::less<char>,
 *               rtv::SetItems<char, std::string, std::less<char> > > dictionary('$');
 *
 *     return 0;
 * }
 * @endcode
 * @subsection usage_iterator Using Trie::Iterator
 * Trie iterator can be used the same way as STL iterator.
 * Key and value can be accessed from iterator using first and secod member.
 * first is vector of key characters and second is a pointer to the value.
 * @code
 * #include <trie.h>
 * #include <string>
 * #include <iostream>
 * #include <vector>
 *
 * int main(int argc, char ** argv) {
 *
 *     rtv::Trie<char, std::string> dictionary('\0');
 *
 *     dictionary.insert("karma$", "Destiny or fate, following as effect from cause");
 *
 *     rtv::Trie<char, std::string>::Iterator iter = dictionary.begin();
 *     rtv::Trie<char, std::string>::Iterator iend = dictionary.end();
 *
 *     for (; iter != iend; ++iter) {
 *
 *         std::cout << iter->first  << " " << iter->second->c_str() << std::endl;
 *     }
 *
 *     return 0;
 * }
 * @endcode
 */
template < typename T,
typename V,
typename Cmp = std::less<T>,
typename Items = SetItems<T, V, Cmp> > class Trie
{
public:
    typedef typename Node<T, V, Cmp, Items>::Iterator Iterator;
    typedef typename Node<T, V, Cmp, Items>::ConstIterator ConstIterator;

public:
    /*!
     * @param endSymbol The symbol which marks the end of key input
     */
    Trie(const T &endSymbol)
            : mRoot(endSymbol) {}

    /*!
     * Add a key with value in to the Trie
     * @param key Key which should be inserted, should be terminated by 'end' symbol
     * @param value The value that is to be set with the key
     * @return An std::pair with pair::first set to the Iterator points to the element and pair::second to true is key is newly inserted, false otherwise
     */
    std::pair<Iterator, bool> insert(const T *key, V const &value) {
        return mRoot.insert(key, value);
    }

    /*!
     * Remove the entry with the given key from the Trie
     * @param key Key which should be erased, should be terminated by 'end' symbol
     * @return true if the given key is erased from the Trie, false otherwise
     */
    bool erase(const T *key) {
        return mRoot.erase(key);
    }

    /*!
     * Remove the entry with the given key from the Trie
     * @param pos Iterator pointing to a single element to be removed from the Trie
     * @return true if the given key is erased form the Trie, false otherwise
     */
    bool erase(Iterator pos) {
        return mRoot.erase(pos);
    }

    /*!
     * Retrieves the value for the given key
     * @param key Key to be searched for, should be terminated by 'end' symbol
     * @return Constant pointer to value for the given key, 0 on failure
     */
    const V *get(const T *key) const {
        return mRoot.get(key);
    }

    /*!
     * Retrieves the value for the given key
     * @param key Key to be searched for, should be terminated by 'end' symbol
     * @return Pointer to value for the given key, 0 on failure
     */
    V *get(const T *key) {
        return mRoot.get(key);
    }

    /*!
     * Retrieves the value for the given key,
     * If key does not match the key of any element in the Trie,
     * the function inserts a new element with that key and returns a reference to its mapped value
     * @param key Key to be searched for, should be terminated by 'end' symbol
     * @return Reference to value for the given key
     */
    V &operator[](const T *key) {
        return *(insert(key, V()).first->second);
    }

    /*!
     * Checks whether the given key is present in the Trie
     * @param key Key to be searched for, should be terminated by 'end' symol
     * @return true if the key is present
     */
    bool hasKey(const T *key) const {
        return mRoot.hasKey(key);
    }

    /*!
     * Test whether Trie is empty
     * @return true if the Trie size is 0, false otherwise
     */
    bool empty() const {
        return mRoot.empty();
    }

    /*!
     * Returns the number of elements in the Trie
     * @return Number of key value pair in the Trie
     */
    unsigned int size() const {
        return mRoot.size();
    }

    /*!
     * All the elements in the Trie are dropped, leaving the Trie with a size of 0.
     */
    void clear() {
        mRoot.clear();
    }

    /*!
     * Retrieves Iterator to the elements with common prefix
     * @param prefix Part of the key which should be searched, should be terminated by 'end' symbol
     * @return Iterator to the elements with prefix specified in 'prefix'
     */
    Iterator startsWith(const T *prefix) {
        return mRoot.startsWith(prefix);
    }

    /*!
     * Retrieves ConstIterator to the elements with common prefix
     * @param prefix Part of the key which should be searched, should be terminated by 'end' symbol
     * @return ConstIterator to the elements with prefix specified in 'prefix'
     */
    ConstIterator startsWith(const T *prefix) const {
        return mRoot.startsWith(prefix);
    }

    /*!
     * Retrieves the end symbol
     * @return end symbol
     */
    T endSymbol() const {
        return mRoot.endSymbol();
    }

    /*!
     * Returns an iterator referring to the first element in the Trie
     * @return An iterator to the first element in the Trie
     */
    Iterator begin() {
        return mRoot.begin();
    }

    /*!
     * Returns an iterator referring to the past-the-end element in the Trie
     * @return An iterator to the element past the end of the Trie
     */
    Iterator end() {
        return mRoot.end();
    }

    /*!
     * Searches the Trie for an element with 'key' as key
     * @param key Key to be searched for, should be terminated by 'end' symbol
     * @return Iterator to the element with key 'key' if found, otherwise an Iterator to Trie::end
     */
    Iterator find(const T *key) {
        return mRoot.find(key);
    }

    /*!
     * Searches the Trie for an element with 'key' as key
     * @param key Key to be searched for, should be terminated by 'end' symbol
     * @return ConstIterator to the element with key 'key' if found, otherwise an ConstIterator to Trie::end
     */
    ConstIterator find(const T *key) const {
        return mRoot.find(key);
    }

    /*!
     * Returns an constant iterator referring to the first element in the Trie
     * @return An constant iterator to the first element in the Trie
     */
    ConstIterator begin() const {
        return mRoot.begin();
    }

    /*!
     * Returns an constant iterator referring to the past-the-end element in the Trie
     * @return An constant iterator to the element past the end of the Trie
     */
    ConstIterator end() const {
        return mRoot.end();
    }

private:
    Trie(Trie const &);
    Trie &operator=(Trie const &);

private:
    Node<T, V, Cmp, Items> mRoot;
};

}

#endif
