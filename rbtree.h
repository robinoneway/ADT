#pragma once

#include <exception>
#include <memory>

class key_not_found : public std::exception {
   public:
    virtual const char *what() const throw() {
        return "Key does not exist in tree";
    }
};

template <typename Key, typename Value>
class rbtree {
   private:
    enum { BLACK = false, RED = true };

    class Node {
       public:
        Key key;                      // key
        Value value;                  // its associated data
        std::unique_ptr<Node> left;   // left...
        std::unique_ptr<Node> right;  // ...and right subtrees
        bool color;                   // color of parent link

        Node(Key key, Value value) {
            this->key = key;
            this->value = value;
            this->color = RED;
        }
    };

    std::unique_ptr<Node> root;  // root of the BST

    Value get(Node *p, Key key);

    Node *getInOrderSuccessorNode(Node *p);

    void DestroyTree(std::unique_ptr<Node> &root);

    /*
     * Returns minimum key of subtree rooted at p
     */
    Key min(Node *p) { return (p->left == nullptr) ? p->key : min(p->left); }

    Key max(Node *p) { return (p->right == nullptr) ? p->key : max(p->right); }

    Node *insert(Node *p, Key key, Value value);

    bool isRed(Node *p) { return (p == nullptr) ? false : (p->color == RED); }

    void colorFlip(Node *p) {
        p->color = !p->color;
        p->left->color = !p->left->color;
        p->right->color = !p->right->color;
    }

    Node *rotateLeft(Node *p);
    Node *rotateRight(Node *p);

    Node *moveRedLeft(Node *p);
    Node *moveRedRight(Node *p);

    Node *deleteMax(Node *p);
    Node *deleteMin(Node *p);

    Node *fixUp(Node *p);

    Node *remove(std::unique_ptr<Node> &p, Key key);

    template <typename Functor>
    void traverse(Functor f, Node *root);

   public:
    // The default unique_ptr constructor sets the uderlying pointer to nullptr

    explicit rbtree() {}

    ~rbtree() { DestroyTree(root); }

    bool contains(Key key) { return get(key) != nullptr; }

    Value get(Key key) { return get(root, key); }

    void put(Key key, Value value) {
        root = insert(root, key, value);
        root->color = BLACK;
    }

    template <typename Functor>
    void traverse(Functor f);

    Key min() { return (root == nullptr) ? nullptr : min(root); }

    Key max() { return (root == nullptr) ? nullptr : max(root); }

    void deleteMin() {
        root = deleteMin(root);
        root->color = BLACK;
    }

    void deleteMax() {
        root = deleteMax(root);
        root->color = BLACK;
    }

    void remove(Key key) {
        if (root == nullptr) return;

        root = remove(root, key);

        if (root != nullptr) {
            root->color = BLACK;
        }
    }
};

/*
 *  Do post order traversal deleting underlying pointer.
 */
//--template<typename Key, typename Value> void rbtree<Key,
//Value>::DestroyTree(Node *current)
template <typename Key, typename Value>
void rbtree<Key, Value>::DestroyTree(std::unique_ptr<Node> &current) {
    if (current.get() == nullptr) return;

    DestroyTree(current->left);
    DestroyTree(current->right);

    current.reset();
}

template <typename Key, typename Value>
typename rbtree<Key, Value>::Node *rbtree<Key, Value>::rotateLeft(Node *p) {
    // Make a right-leaning 3-node lean to the left.
    Node *x = p->right;

    p->right = x->left;

    x->left = p;

    x->color = x->left->color;

    x->left->color = RED;

    return x;
}

template <typename Key, typename Value>
typename rbtree<Key, Value>::Node *rbtree<Key, Value>::rotateRight(Node *p) {
    // Make a left-leaning 3-node lean to the right.
    Node *x = p->left;

    p->left = x->right;

    x->right = p;

    x->color = x->right->color;

    x->right->color = RED;

    return x;
}

template <typename Key, typename Value>
typename rbtree<Key, Value>::Node *rbtree<Key, Value>::moveRedLeft(Node *p) {
    // Assuming that p is red and both p->left and p->left->left
    // are black, make p->left or one of its children red
    colorFlip(p);

    if (isRed(p->right->left)) {
        p->right = rotateRight(p->right);

        p = rotateLeft(p);

        colorFlip(p);
    }
    return p;
}

template <typename Key, typename Value>
typename rbtree<Key, Value>::Node *rbtree<Key, Value>::moveRedRight(Node *p) {
    // Assuming that p is red and both p->right and p->right->left
    // are black, make p->right or one of its children red
    colorFlip(p);

    if (isRed(p->left->left)) {
        p = rotateRight(p);
        colorFlip(p);
    }
    return p;
}

template <typename Key, typename Value>
typename rbtree<Key, Value>::Node *rbtree<Key, Value>::fixUp(Node *p) {
    if (isRed(p->right)) p = rotateLeft(p);

    if (isRed(p->left) && isRed(p->left->left)) p = rotateRight(p);

    if (isRed(p->left) && isRed(p->right))  // four node
        colorFlip(p);

    return p;
}

template <typename Key, typename Value>
typename rbtree<Key, Value>::Node *rbtree<Key, Value>::deleteMax(Node *p) {
    if (isRed(p->left)) p = rotateRight(p);

    if (p->right == nullptr) return nullptr;

    if (!isRed(p->right) && !isRed(p->right->left)) p = moveRedRight(p);

    p->right = deleteMax(p->right);

    return fixUp(p);
}

template <typename Key, typename Value>
typename rbtree<Key, Value>::Node *rbtree<Key, Value>::deleteMin(Node *p) {
    if (p->left == nullptr) {
        // http://www.teachsolaisgames.com/articles/balanced_left_leaning.html,
        // another C++ implementation, that p's underlying pointer must be
        // deleted.
        p.reset();
        return nullptr;
    }

    if (!isRed(p->left) && !isRed(p->left->left)) p = moveRedLeft(p);

    p->left = deleteMin(p->left);

    return fixUp(p);
}

template <typename Key, typename Value>
//--typename rbtree<Key, Value>::Node *rbtree<Key, Value>::remove(Node *p, Key
//key)
typename rbtree<Key, Value>::Node *rbtree<Key, Value>::remove(
    std::unique_ptr<Node> &p, Key key) {
    if (key < p->key) {
        if (!isRed(p->left) && !isRed(p->left->left)) {
            p = moveRedLeft(p);
        }

        p->left = remove(p->left, key);

    } else {
        if (isRed(p->left)) {
            p = rotateRight(p);
        }

        if ((key == p->key) && (p->right == nullptr)) {
            /* From code at
             * http://www.teachsolaisgames.com/articles/balanced_left_leaning.html
             * Taken from the LeftLeaningRedBlack::DeleteRec method
             */
            p.reset();
            return nullptr;
        }

        if (!isRed(p->right) && !isRed(p->right->left)) {
            p = moveRedRight(p);
        }

        if (key == p->key) {
            /* added instead of code above */
            Node *successor = getInOrderSuccessorNode(p);
            p->value =
                successor->value;  // Assign p in-order successor key and value
            p->key = successor->key;

            p->right = deleteMin(p->right);

        } else {
            p->right = remove(p->right, key);
        }
    }

    return fixUp(p);
}

/*
 * Returns key's associated value. The search for key starts in the subtree
 * rooted at p.
 */
template <typename Key, typename Value>
Value rbtree<Key, Value>::get(Node *p, Key key) {
    /* alternate recursive code
       if (p == 0) {   ValueNotFound(key);}
       if (key == p->key) return p->value;
       if (key < p->key)  return get(p->left,  key);
       else              return get(p->right, key);
    */
    // non-recursive version
    while (p != nullptr) {
        if (key < p->key)
            p = p->left;
        else if (key > p->key)
            p = p->right;
        else
            return p->value;
    }

    throw key_not_found();
}

template <typename Key, typename Value>
inline typename rbtree<Key, Value>::Node *
rbtree<Key, Value>::getInOrderSuccessorNode(rbtree<Key, Value>::Node *p) {
    p = p->right;

    while (p->left != nullptr) {
        p = p->left;
    }

    return p;
}

template <typename Key, typename Value>
typename rbtree<Key, Value>::Node *rbtree<Key, Value>::insert(
    rbtree<Key, Value>::Node *p, Key key, Value value) {
    if (p == nullptr) return std::make_unique<Node>(key, value);

    /* We view the left-leaning red black tree as a 2 3 4 tree. So first check
     * if p is a 4 node and needs to be "split" by flipping colors.  */
    if (isRed(p->left) && isRed(p->right)) colorFlip(p);

    if (key == p->key) /* if key already exists, overwrite its value */
        p->value = value;
    else if (key < p->key) /* otherwise recurse */
        p->left = insert(p->left, key, value);
    else
        p->right = insert(p->right, key, value);

    /* rebalance tree */
    if (isRed(p->right)) p = rotateLeft(p);

    if (isRed(p->left) && isRed(p->left->left)) p = rotateRight(p);

    return p;
}
template <typename Key, typename Value>
template <typename Functor>
inline void rbtree<Key, Value>::traverse(Functor f) {
    return traverse(f, root);
}

/* in order traversal */
template <typename Key, typename Value>
template <typename Functor>
void rbtree<Key, Value>::traverse(Functor f, rbtree<Key, Value>::Node *root) {
    if (root == nullptr) {
        return;
    }

    traverse(f, root->left);

    f(root->value);

    traverse(f, root->right);
}
