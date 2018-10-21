#pragma once

#include <exception>
#include <memory>

class KeyNotFound : public std::exception {
public:
    virtual const char *what() const throw() {
        return "Key does not exist in tree";
    }
};
template <typename Key, typename Value>
class RBTree {
private:
    enum { BLACK = false, RED = true };

    class Node {
    public:
        Key key;      // key
        Value value;  // its associated data
        Node *left;   // left...
        Node *right;  // ...and right subtrees
        bool color;   // color of parent link

        Node(Key key, Value value) {
            this->key = key;
            this->value = value;
            this->color = RED;
        }
    };

    Node *root;  // root of the BST

    Value Get(Node *p, Key key);

    Node *GetInOrderSuccessorNode(Node *p);

    void DestroyTree(Node *root);

    /*
     * Returns Minimum key of subtree rooted at p
     */
    Key Min(Node *p) { return (p->left == nullptr) ? p->key : Min(p->left); }

    Key Max(Node *p) { return (p->right == nullptr) ? p->key : Max(p->right); }

    Node *Insert(Node *p, Key key, Value value);

    bool IsRed(Node *p) { return (p == nullptr) ? false : (p->color == RED); }

    void ColorFlip(Node *p) {
        p->color = !p->color;
        p->left->color = !p->left->color;
        p->right->color = !p->right->color;
    }

    Node *RotateLeft(Node *p);
    Node *RotateRight(Node *p);

    Node *MoveRedLeft(Node *p);
    Node *MoveRedRight(Node *p);

    Node *DeleteMax(Node *p);
    Node *DeleteMin(Node *p);

    Node *FixUp(Node *p);

    Node *Remove(Node *p, Key key);

    template <typename Functor>
    void Traverse(Functor f, Node *root);

public:
    // The default unique_ptr constructor sets the uderlying pointer to nullptr

    explicit RBTree() {}

    ~RBTree() { DestroyTree(root); }

    bool Contains(Key key) { return Get(key) != nullptr; }

    Value Get(Key key) { return Get(root, key); }

    void Put(Key key, Value value) {
        root = Insert(root, key, value);
        root->color = BLACK;
    }

    template <typename Functor>
    void Traverse(Functor f);

    Key Min() { return (root == nullptr) ? nullptr : Min(root); }

    Key Max() { return (root == nullptr) ? nullptr : Max(root); }

    void DeleteMin() {
        root = DeleteMin(root);
        root->color = BLACK;
    }

    void DeleteMax() {
        root = DeleteMax(root);
        root->color = BLACK;
    }

    void Remove(Key key) {
        if (root == nullptr) return;

        root = Remove(root, key);

        if (root != nullptr) {
            root->color = BLACK;
        }
    }
};

/*
 *  Do post order traversal deleting underlying pointer.
 */
//--template<typename Key, typename Value> void RBTree<Key,
// Value>::DestroyTree(Node *current)
template <typename Key, typename Value>
void RBTree<Key, Value>::DestroyTree(Node *current) {
    if (current == nullptr) return;

    DestroyTree(current->left);
    DestroyTree(current->right);
    if (current) {
        delete current;
        current = nullptr;
    }
}

template <typename Key, typename Value>
typename RBTree<Key, Value>::Node *RBTree<Key, Value>::RotateLeft(Node *p) {
    // Make a right-leaning 3-node lean to the left.
    Node *x = p->right;

    p->right = x->left;

    x->left = p;

    x->color = x->left->color;

    x->left->color = RED;

    return x;
}

template <typename Key, typename Value>
typename RBTree<Key, Value>::Node *RBTree<Key, Value>::RotateRight(Node *p) {
    // Make a left-leaning 3-node lean to the right.
    Node *x = p->left;

    p->left = x->right;

    x->right = p;

    x->color = x->right->color;

    x->right->color = RED;

    return x;
}

template <typename Key, typename Value>
typename RBTree<Key, Value>::Node *RBTree<Key, Value>::MoveRedLeft(Node *p) {
    // AssuMing that p is red and both p->left and p->left->left
    // are black, make p->left or one of its children red
    ColorFlip(p);

    if (IsRed(p->right->left)) {
        p->right = RotateRight(p->right);

        p = RotateLeft(p);

        ColorFlip(p);
    }
    return p;
}

template <typename Key, typename Value>
typename RBTree<Key, Value>::Node *RBTree<Key, Value>::MoveRedRight(Node *p) {
    // AssuMing that p is red and both p->right and p->right->left
    // are black, make p->right or one of its children red
    ColorFlip(p);

    if (IsRed(p->left->left)) {
        p = RotateRight(p);
        ColorFlip(p);
    }
    return p;
}

template <typename Key, typename Value>
typename RBTree<Key, Value>::Node *RBTree<Key, Value>::FixUp(Node *p) {
    if (IsRed(p->right)) p = RotateLeft(p);

    if (IsRed(p->left) && IsRed(p->left->left)) p = RotateRight(p);

    if (IsRed(p->left) && IsRed(p->right))  // four node
        ColorFlip(p);

    return p;
}

template <typename Key, typename Value>
typename RBTree<Key, Value>::Node *RBTree<Key, Value>::DeleteMax(Node *p) {
    if (IsRed(p->left)) p = RotateRight(p);

    if (p->right == nullptr) return nullptr;

    if (!IsRed(p->right) && !IsRed(p->right->left)) p = MoveRedRight(p);

    p->right = DeleteMax(p->right);

    return FixUp(p);
}

template <typename Key, typename Value>
typename RBTree<Key, Value>::Node *RBTree<Key, Value>::DeleteMin(Node *p) {
    if (p->left == nullptr) {
        // http://www.teachsolaisgames.com/articles/balanced_left_leaning.html,
        // another C++ implementation, that p's underlying pointer must be
        // deleted.
        if (p) {
            delete p;
            p = nullptr;
        }
        return nullptr;
    }

    if (!IsRed(p->left) && !IsRed(p->left->left)) p = MoveRedLeft(p);

    p->left = DeleteMin(p->left);

    return FixUp(p);
}

template <typename Key, typename Value>
//--typename RBTree<Key, Value>::Node *RBTree<Key, Value>::Remove(Node *p, Key
// key)
typename RBTree<Key, Value>::Node *RBTree<Key, Value>::Remove(Node *p,
                                                              Key key) {
    if (key < p->key) {
        if (!IsRed(p->left) && !IsRed(p->left->left)) {
            p = MoveRedLeft(p);
        }

        p->left = Remove(p->left, key);

    } else {
        if (IsRed(p->left)) {
            p = RotateRight(p);
        }

        if ((key == p->key) && (p->right == nullptr)) {
            /* From code at
             * http://www.teachsolaisgames.com/articles/balanced_left_leaning.html
             * Taken from the LeftLeaningRedBlack::DeleteRec method
             */
            if (p) {
                delete p;
                p = nullptr;
            }
            return nullptr;
        }

        if (!IsRed(p->right) && !IsRed(p->right->left)) {
            p = MoveRedRight(p);
        }

        if (key == p->key) {
            /* added instead of code above */
            Node *successor = GetInOrderSuccessorNode(p);
            p->value =
                successor->value;  // Assign p in-order successor key and value
            p->key = successor->key;

            p->right = DeleteMin(p->right);

        } else {
            p->right = Remove(p->right, key);
        }
    }

    return FixUp(p);
}

/*
 * Returns key's associated value. The search for key starts in the subtree
 * rooted at p.
 */
template <typename Key, typename Value>
Value RBTree<Key, Value>::Get(Node *p, Key key) {
    /* alternate recursive code
       if (p == 0) {   ValueNotFound(key);}
       if (key == p->key) return p->value;
       if (key < p->key)  return Get(p->left,  key);
       else              return Get(p->right, key);
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

    throw KeyNotFound();
}

template <typename Key, typename Value>
inline typename RBTree<Key, Value>::Node *
RBTree<Key, Value>::GetInOrderSuccessorNode(RBTree<Key, Value>::Node *p) {
    p = p->right;

    while (p->left != nullptr) {
        p = p->left;
    }

    return p;
}

template <typename Key, typename Value>
typename RBTree<Key, Value>::Node *RBTree<Key, Value>::Insert(
    RBTree<Key, Value>::Node *p, Key key, Value value) {
    if (p == nullptr) {
        return new Node(key, value);
    }

    /* We view the left-leaning red black tree as a 2 3 4 tree. So first check
     * if p is a 4 node and needs to be "split" by flipping colors.  */
    if (IsRed(p->left) && IsRed(p->right)) ColorFlip(p);

    if (key == p->key) /* if key already exists, overwrite its value */
        p->value = value;
    else if (key < p->key) /* otherwise recurse */
        p->left = Insert(p->left, key, value);
    else
        p->right = Insert(p->right, key, value);

    /* rebalance tree */
    if (IsRed(p->right)) p = RotateLeft(p);

    if (IsRed(p->left) && IsRed(p->left->left)) p = RotateRight(p);

    return p;
}
template <typename Key, typename Value>
template <typename Functor>
inline void RBTree<Key, Value>::Traverse(Functor f) {
    return Traverse(f, root);
}

/* in order traversal */
template <typename Key, typename Value>
template <typename Functor>
void RBTree<Key, Value>::Traverse(Functor f, RBTree<Key, Value>::Node *root) {
    if (root == nullptr) {
        return;
    }

    Traverse(f, root->left);

    f(root->value);

    Traverse(f, root->right);
}
