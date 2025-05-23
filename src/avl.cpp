#include <assert.h>
#include "avl.h"

static uint32_t max(uint32_t lhs, uint32_t rhs) {
    return lhs < rhs ? rhs : lhs;
}

static void avl_update(AVLNode *node) {
    node->height = 1 + max(avl_height(node->left), avl_height(node->right));
    node->cnt = 1 + avl_cnt(node->left) + avl_cnt(node->right);
}

static AVLNode *rot_left(AVLNode *node) {
    AVLNode * parent   = node->parent;
    AVLNode * new_node = node->right;
    AVLNode * inner    = new_node->left;
    node->right = inner;
    if (inner) inner->parent = node;
    new_node->parent = parent;
    new_node->left = node;
    node->parent = new_node;
    avl_update(node);
    avl_update(new_node);
    return new_node;
}

static AVLNode *rot_right(AVLNode *node) {
    AVLNode * parent   = node->parent;
    AVLNode * new_node = node->left;
    AVLNode * inner    = new_node->right;
    node->left = inner;
    if (inner) inner->parent = node;
    new_node->parent = parent;
    new_node->right = node;
    node->parent = new_node;
    avl_update(node);
    avl_update(new_node);
    return new_node;
}

static AVLNode *avl_fix_left(AVLNode *node) {
    if (avl_height(node->left->left) < avl_height(node->left->right))
        node->left = rot_left(node->left);
    return rot_right(node);
}

static AVLNode *avl_fix_right(AVLNode *node) {
    if (avl_height(node->right->right) < avl_height(node->right->left))
        node->right = rot_right(node->right);
    return rot_left(node);
}

// Fonction de rééquilibrage d’un nœud dans un arbre AVL.
// Elle remonte depuis 'node' jusqu'à la racine en mettant à jour les hauteurs
// et en effectuant les rotations nécessaires pour garantir l'équilibre AVL.
AVLNode *avl_fix(AVLNode * node) {
    while(true) {
        AVLNode **from = &node;
        AVLNode *parent = node->parent;

        // Si le nœud a un parent, on détermine si le nœud est à gauche ou à droite
        // Cela permet de savoir quel champ du parent (left ou right) on devra modifier
        if (parent) from = parent->left == node ? &parent->left : &parent->right;

        // Mettre le neoud à jour
        avl_update(node);

        uint32_t l = avl_height(node->left);
        uint32_t r = avl_height(node->right);

        // Cas de déséquilibre à gauche (le sous-arbre gauche est trop profond)
        if (l == r + 2) *from = avl_fix_left(node);

        // Cas de déséquilibre à droite (le sous-arbre droit est trop profond)
        else if (r == l + 2) *from = avl_fix_right(node);

        // Si on est remonté jusqu'à la racine, on retourne le nouveau sous-arbre équilibré
        if (!parent) return *from;

        // Sinon, on continue à remonter dans l’arbre
        node = parent;
    }
}

// Detach a node with at most 1 child
static AVLNode * avl_del_easy(AVLNode * node) {
    assert(!node->left || !node->right);
    AVLNode * child = node->left ? node->left : node->right;
    AVLNode * parent = node->parent;

    if (child) child->parent = parent;
    if(!parent) return child;

    AVLNode **from = parent->left == node ? &parent->left : &parent->right;
    *from = child;

    return avl_fix(parent);
}

// Complete delete logic
AVLNode * avl_del(AVLNode * node) {
    // If the node has at most 1 child, easy case
    if (!node->left || !node->right) return avl_del_easy(node);

    // Find the minimum of the right tree
    // which is the most left node
    AVLNode *victim = node->right;
    while(victim->left) victim = victim->left;

    // Detach it from the tree
    AVLNode *root = avl_del_easy(victim);
    
    *victim = *node;
    if (victim->left) victim->left->parent = victim;
    if (victim->right) victim->right->parent = victim;

    AVLNode **from = &root;
    AVLNode *parent = node->parent;

    if (parent) from = parent->left == node ? &parent->left : &parent->right;
    *from = victim;
    return root;
}