#ifndef VECTOR_SET_H
#define VECTOR_SET_H

/**
\file vector_set.h
*/

/**
\defgroup vector_set Data structures for manipulating sets of vectors.
*/
//@{

typedef struct vector_domain *vdom_t;

typedef struct vector_set *vset_t;

typedef struct vector_relation *vrel_t;

/**
\brief Create a domain.

\param n The length of vectors in the domain.
*/
extern vdom_t vdom_create(int n);

/**
\brief Create a set.

\param k If non-zero this indicates the length of the sub-domain.
\param proj If non-NULL this is a sorted list of the indices of the sub-domain.
*/
extern vset_t vset_create(vdom_t dom,int k,int* proj);

/**
\brief Add an element to a set.
*/
extern void vset_add(vset_t set,int* e);

/**
\brief Test if an element is a member.
*/
extern int vset_member(vset_t set,int* e);

/**
\brief Test if a set is empty.
*/
extern int vset_is_empty(vset_t set);

/**
\brief Remove all elements from a set.
*/
extern void vset_clear(vset_t set);

/**
\brief Callback for set enumeration.
*/
typedef void(*vset_element_cb)(void*context,int *e);

/**
\brief Enumerate the elements of a set.

For each element of the given set, the given callback with be called with as arguments
the given context and the set element.
*/
extern void vset_enum(vset_t set,vset_element_cb cb,void* context);

/**
\brief Copy a vset.
*/
extern void vset_copy(vset_t dst,vset_t src);

/**
\brief Project src down to dst.
*/
extern void vset_project(vset_t dst,vset_t src);

/**
\brief dst := dst U src
*/
extern void vset_union(vset_t dst,vset_t src);

/**
\brief dst := dst \\ src
*/
extern void vset_minus(vset_t dst,vset_t src);

/**
\brief (dst,src) := (dst U src,src \\ dst)
*/
extern void vset_zip(vset_t dst,vset_t src);

/**
\brief Count the number of diagram nodes and the number of elements stored.
*/
extern void vset_count(vset_t set,long *nodes,long long *elements);

/**
\brief Create a relation
*/
extern vrel_t vrel_create(vdom_t dom,int k,int* proj);

/**
\brief Add an element to a relation.
*/
extern void vrel_add(vrel_t rel,int* src,int *dst);

/**
\brief dst := { y | exists x in src : x rel y }
*/
extern void vset_next(vset_t dst,vset_t src,vrel_t rel);

//@}


#endif
