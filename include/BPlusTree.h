#ifndef BPLUSTREE_H
#define BPLUSTREE_H

#include <iostream>
#include <vector>
#include <cstring>
#include "MulticastReceiver.h"

template <typename T>
class BPlusTreeNode
{
public:
    std::vector<T> keys;
    std::vector<BPlusTreeNode *> children;
    bool isLeaf;

    BPlusTreeNode(bool leaf);
};

template <typename T>
class BPlusTree
{
private:
    int degree;
    BPlusTreeNode<T> *root;

    void splitChild(BPlusTreeNode<T> *parent, int childIndex);
    void insertIntoLeaf(BPlusTreeNode<T> *leaf, const T &key);
    void insertIntoNonLeaf(BPlusTreeNode<T> *node, int index, const T &key);
    void insertNonFull(BPlusTreeNode<T> *node, const T &key);

public:
    BPlusTree(int d);
    void insert(const T &key);
    T getMin();
    void deleteMin();
    bool isEmpty() const;
};

#endif // BPLUSTREE_H
