#include "BPlusTree.h"

template <typename T>
BPlusTreeNode<T>::BPlusTreeNode(bool leaf) : isLeaf(leaf) {}

template <typename T>
BPlusTree<T>::BPlusTree(int d) : degree(d), root(nullptr) {}

template <typename T>
void BPlusTree<T>::splitChild(BPlusTreeNode<T> *parent, int childIndex)
{
    BPlusTreeNode<T> *fullChild = parent->children[childIndex];
    BPlusTreeNode<T> *newChild = new BPlusTreeNode<T>(fullChild->isLeaf);

    int mid = degree / 2;
    for (int i = mid; i < degree; ++i)
    {
        newChild->keys.push_back(fullChild->keys[i]);
    }

    if (!fullChild->isLeaf)
    {
        for (int i = mid + 1; i <= degree; ++i)
        {
            newChild->children.push_back(fullChild->children[i]);
            fullChild->children[i] = nullptr;
        }
    }

    fullChild->keys.resize(mid);
    if (!fullChild->isLeaf)
    {
        fullChild->children.resize(mid + 1);
    }

    parent->keys.insert(parent->keys.begin() + childIndex, fullChild->keys[mid]);
    parent->children.insert(parent->children.begin() + childIndex + 1, newChild);
}

template <typename T>
void BPlusTree<T>::insertIntoLeaf(BPlusTreeNode<T> *leaf, const T &key)
{
    int i = 0;
    while (i < leaf->keys.size() && key.sequenceNumber > leaf->keys[i].sequenceNumber)
    {
        ++i;
    }
    leaf->keys.insert(leaf->keys.begin() + i, key);
}

template <typename T>
void BPlusTree<T>::insertIntoNonLeaf(BPlusTreeNode<T> *node, int index, const T &key)
{
    while (index > 0 && key.sequenceNumber < node->keys[index - 1].sequenceNumber)
    {
        node->keys[index] = node->keys[index - 1];
        node->children[index + 1] = node->children[index];
        --index;
    }
    node->keys[index] = key;
    node->children[index + 1] = node->children[index];
}

template <typename T>
void BPlusTree<T>::insertNonFull(BPlusTreeNode<T> *node, const T &key)
{
    if (node->isLeaf)
    {
        insertIntoLeaf(node, key);
        if (node->keys.size() == degree)
        {
            BPlusTreeNode<T> *parent = node->children[0];
            splitChild(parent, 0);
        }
    }
    else
    {
        int i = 0;
        while (i < node->keys.size() && key.sequenceNumber > node->keys[i].sequenceNumber)
        {
            ++i;
        }
        if (node->children[i]->keys.size() == degree)
        {
            splitChild(node, i);
            if (key.sequenceNumber > node->keys[i].sequenceNumber)
            {
                ++i;
            }
        }
        insertNonFull(node->children[i], key);
    }
}

template <typename T>
void BPlusTree<T>::insert(const T &key)
{
    if (root == nullptr)
    {
        root = new BPlusTreeNode<T>(true);
        root->keys.push_back(key);
        return;
    }

    if (root->keys.size() == degree)
    {
        BPlusTreeNode<T> *newRoot = new BPlusTreeNode<T>(false);
        newRoot->children.push_back(root);
        splitChild(newRoot, 0);
        root = newRoot;
    }
    insertNonFull(root, key);
}

template <typename T>
T BPlusTree<T>::getMin()
{
    BPlusTreeNode<T> *curr = root;
    while (!curr->isLeaf)
    {
        curr = curr->children[0];
    }
    return curr->keys[0];
}

template <typename T>
void BPlusTree<T>::deleteMin()
{
    BPlusTreeNode<T> *curr = root;
    while (!curr->isLeaf)
    {
        curr = curr->children[0];
    }

    curr->keys.erase(curr->keys.begin());

    if (curr->keys.empty() && !curr->isLeaf)
    {
        BPlusTreeNode<T> *parent = curr->children[0];
        parent->children.erase(parent->children.begin());
        delete curr;
    }
}

template <typename T>
bool BPlusTree<T>::isEmpty() const
{
    return root == nullptr || (root->isLeaf && root->keys.empty());
}

// 显示实例化
// template class BPlusTree<T>;

#include "BPlusTree.h"

int main()
{
    BPlusTree<Message> tree(3);

    Message msg1(MessageType::INIT, 1, 0, "Message 1");
    Message msg2(MessageType::DATA, 2, 0, "T 2");

    tree.insert(msg1);
    tree.insert(msg2);

    Message minMsg = tree.getMin();
    std::cout << "Min sequenceNumber: " << minMsg.sequenceNumber << std::endl;

    tree.deleteMin();

    minMsg = tree.getMin();
    std::cout << "Min sequenceNumber after deletion: " << minMsg.sequenceNumber << std::endl;

    return 0;
}
