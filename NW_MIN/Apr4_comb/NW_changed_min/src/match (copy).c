
//Modified by Avinash Ramu, University of FLorida.
//  Usage = ./nw_match seed_file tree_file freq
/* 

Copyright (c) 2009 Thomas Junier and Evgeny Zdobnov, University of Geneva
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

* Redistributions of source code must retain the above copyright notice,
    this list of conditions and the following disclaimer.
* Redistributions in binary form must reproduce the above copyright notice,
    this list of conditions and the following disclaimer in the documentation
    and/or other materials provided with the distribution.
* Neither the name of the University of Geneva nor the names of its
    contributors may be used to endorse or promote products derived from this
    software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

*/
/* match.c: match a tree to a pattern tree (subgraph) */

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>

/*
#include <assert.h>
#include <math.h>

#include "node_set.h"
*/
#include "parser.h"
#include "to_newick.h"
#include "tree.h"
#include "order_tree.h"
#include "hash.h"
#include "list.h"
#include "rnode.h"
#include "link.h"
#include "nodemap.h"
#include "common.h"
#include "rnode_iterator.h"
#include "masprintf.h"

#define LABEL_LENGTH 200
#define TREE_COUNT 400
#define MAST_LENGTH 20000

void newick_scanner_set_string_input(char *);
void newick_scanner_clear_string_input();
void newick_scanner_set_file_input(FILE *);

struct parameters {
	char *pattern;
	//FILE *target_trees;
	char* target_trees;
	int reverse;
	char separator;
	struct llist 	*labels;
};

void help(char* argv[])
{
	printf(
"Matches a tree to a pattern tree\n"
"\n"
"Synopsis\n"
"--------\n"
"%s [-v] <target tree filename|-> <pattern tree>\n"
"\n"
"Input\n"
"-----\n"
"\n"
"The first argument is the name of the file containing the target tree (to\n"
"which support values are to be attributed), or '-' (in which case the tree\n"
"is read on stdin).\n"
"\n"
"The second argument is a pattern tree\n"
"\n"
"Output\n"
"------\n"
"\n"
"Outputs the target tree if the pattern tree is a subgraph of it.\n"
"\n"
"Options\n"
"-------\n"
"\n"
"    -v: prints tree which do NOT match the pattern.\n"
"\n"
"Limits & Assumptions\n"
"--------------------\n"
"\n"
"Assumes that the labels are leaf labels, and that they are unique in\n"
"all trees (both target and pattern)\n"
"\n"
"Example\n"
"-------\n"
"\n"
"# Prints trees in data/vrt_gen.nw where Tamias is closer to Homo than it is\n"
"# to Vulpes:\n"
"$ %s data/vrt_gen.nw '((Tamias,Homo),Vulpes);'\n"
"\n"
"# Prints trees in data/vrt_gen.nw where Tamias is NOT closer to Homo than it is\n"
"# to Vulpes:\n"
"$ %s -v data/vrt_gen.nw '((Tamias,Homo),Vulpes);'\n",

	argv[0],
	argv[0],
	argv[0]
	      );
}

struct parameters get_params(char* target, char* pattern)
{
	struct parameters params;
	char opt_char;
	params.reverse = FALSE;

	/* parse options and switches */
	/*while ((opt_char = getopt(argc, argv, "hv")) != -1) {
		switch (opt_char) {
		case 'h':
			help(argv);
			exit(EXIT_SUCCESS);
		/* we keep this for debugging, but not documented */
		/*case 'v':
			params.reverse = TRUE;
			break;
		}
	}
	/* get arguments */
	/*if (2 == (argc - optind))	{
		if (0 != strcmp("-", argv[optind])) {
			FILE *fin = fopen(argv[optind], "r");
			if (NULL == fin) {
				perror(NULL);
				exit(EXIT_FAILURE);
			}
			params.target_trees = fin;
		} else {
			params.target_trees = stdin;
		}
		params.pattern = argv[optind+1];
	} else {
		fprintf(stderr, "Usage: %s [-hv] <target trees filename|-> <pattern>\n", argv[0]);
		exit(EXIT_FAILURE);
	}*/

	params.target_trees = target;
	params.pattern = pattern;
	params.separator = ' ';
	return params;
}

/* A debugging function */

void show_node_children_numbers(struct rooted_tree *tree)
{
	struct list_elem *el;
	for (el = tree->nodes_in_order->head; NULL != el; el = el->next) {
		struct rnode *current = el->data;
		printf ("node %p (%s): %d children\n", current,
			current->label, children_count(current));
	}
}

/* Get pattern tree and order it */

struct rooted_tree *get_ordered_pattern_tree(char *pattern)
{
	struct rooted_tree *pattern_tree;

	newick_scanner_set_string_input(pattern);
	pattern_tree = parse_tree();
	if (NULL == pattern_tree) {
		fprintf (stderr, "Could not parse pattern tree '%s'\n", pattern);
		printf("\nError4");
		exit(EXIT_FAILURE);
	}
	newick_scanner_clear_string_input();

	if (!order_tree_lbl(pattern_tree)) { perror(NULL); exit(EXIT_FAILURE); }

	return pattern_tree;
}

/* We only consider leaf labels. This might change if keeping internal labels
 * proves useful. */

void remove_inner_node_labels(struct rooted_tree *target_tree)
{
	struct list_elem *el;

	for (el=target_tree->nodes_in_order->head; NULL != el; el=el->next) 
	{
		struct rnode *current = el->data;
		if (is_leaf(current)) continue;
		free(current->label);
		// We need to allocate dynamically, since this will later be
		// passed to free().
		current->label = strdup("");
	}
}

/* Removes all nodes in target tree whose labels are not found in the 'kept'
 * hash */

void prune_extra_labels(struct rooted_tree *target_tree, struct hash *kept)
{
	struct list_elem *el;

	for (el=target_tree->nodes_in_order->head; NULL != el; el=el->next) {
		struct rnode *current = el->data;
		char *label = current->label;
		if (0 == strcmp("", label)) continue;
		if (is_root(current)) continue;
		if (NULL == hash_get(kept, current->label)) {
			/* not in 'kept': remove */
			enum unlink_rnode_status result = unlink_rnode(current);
			switch(result) {
			case UNLINK_RNODE_DONE:
				break;
			case UNLINK_RNODE_ROOT_CHILD:
				/* TODO: shouldn't we do this?
				unlink_rnode_root_child->parent = NULL;
				target_tree->root = unlink_rnode_root_child;
				*/
				break;
			case UNLINK_RNODE_ERROR:
				fprintf (stderr, "Memory error - "
						"exiting.\n");
				exit(EXIT_FAILURE);
			default:
				assert(0); /* programmer error */
			}
		}
	}

	destroy_llist(target_tree->nodes_in_order);
	target_tree->nodes_in_order = get_nodes_in_order(target_tree->root);
	reset_current_child_elem(target_tree);
}

void prune_empty_labels(struct rooted_tree *target_tree)
{
	struct list_elem *el;
	for (el=target_tree->nodes_in_order->head; NULL != el; el=el->next) {
		struct rnode *current = el->data;
		char *label = current->label;
		if (is_leaf(current)) {
			if (0 == strcmp("", label)) {
				enum unlink_rnode_status result =
					unlink_rnode(current);
				switch(result) 
				{
				case UNLINK_RNODE_DONE:
					break;
				case UNLINK_RNODE_ROOT_CHILD:
					/* TODO: shouldn't we do this?
					unlink_rnode_root_child->parent = NULL;
					target_tree->root = unlink_rnode_root_child;
					*/
					break;
				case UNLINK_RNODE_ERROR:
					perror(NULL);
					exit(EXIT_FAILURE);
				default:
					assert(0); /* programmer error */
				}
			}
		}
	}
}

void remove_branch_lengths(struct rooted_tree *target_tree)
{
	struct list_elem *el;

	for (el = target_tree->nodes_in_order->head; NULL != el; el = el->next) {
		struct rnode *current = el->data;
		if (strcmp("", current->edge_length_as_string) != 0) {
			free(current->edge_length_as_string);
			// We need to allocate dynamically, since this will
			// later be passed to free():
			// WRONG! cur_edge->length_as_string = ""
			current->edge_length_as_string = strdup("");
		}
	}
}

void remove_knee_nodes(struct rooted_tree *tree)
{
	/* tree was modified -> can't use its ordered node list */
	struct llist *nodes_in_order = get_nodes_in_order(tree->root);
	if (NULL == nodes_in_order) { perror(NULL); exit(EXIT_FAILURE); }
	struct list_elem *el;

	for (el = nodes_in_order->head; NULL != el; el = el->next) {
		struct rnode *current = el->data;
		if (is_inner_node(current))
			if (1 == children_count(current))
				if (! splice_out_rnode(current)) {
					perror(NULL);
					exit(EXIT_FAILURE);
				}
	}
	destroy_llist(nodes_in_order);

	/* If the root has only one child, make that child the new root */
	if (1 == children_count(tree->root)) {
		struct rnode *roots_first_child = tree->root->children->head->data;
		tree->root = roots_first_child;
	}
}

int process_tree(struct rooted_tree *tree, struct hash *pattern_labels,
		char *pattern_newick)
{
	/* NOTE: whenever I alter the tree structure, I rebuild nodes_in_order
	 * as soon as possible. Then I no longer need to guard against this
	 * list being invalid. WARNING: I did this just enough to make all
	 * tests pass, NOT systemytically after each tree-function call. It may
	 * be necessary to do it more thoroughly later on. */
	char *original_newick = to_newick(tree->root);
	//printf ("%s = original newick\n", original_newick);
	remove_inner_node_labels(tree);
	prune_extra_labels(tree, pattern_labels);
	prune_empty_labels(tree);
	remove_knee_nodes(tree);
	remove_branch_lengths(tree);	
	if (! order_tree_lbl(tree)) { perror(NULL); exit(EXIT_FAILURE); }
	char *processed_newick = to_newick(tree->root);
	//printf ("%s = processed newick\n", processed_newick);
	//printf ("%s = pattern newick\n", pattern_newick);
	int match = (0 == strcmp(processed_newick, pattern_newick));
	//printf("\nmatch is %d",match);
	//match = params.reverse ? !match : match;
	//if (match) printf ("%s\n", original_newick);
	free(processed_newick);
	free(original_newick);
	return match;
}

int match_pattern(char* target, char* pattern)
{
	struct rooted_tree *pattern_tree;	
	struct rooted_tree *tree;	
	char *pattern_newick;
	struct hash *pattern_labels;

	//struct parameters params = get_params(target, pattern);

	pattern_tree = get_ordered_pattern_tree(pattern);
	pattern_newick = to_newick(pattern_tree->root);
	//printf ("match%s = pattern newick \n", pattern_newick);
	pattern_labels = create_label2node_map(pattern_tree->nodes_in_order);

	/* get_ordered_pattern_tree() causes a tree to be read from a string,
	 * which means that we must now tell the lexer to change its input
	 * source. It's not enough to just set the external FILE pointer
	 * 'nwsin' to standard input or the user-supplied file, apparently:
	 * this would segfault. */
	//newick_scanner_set_file_input(params.target_trees);
	tree = get_ordered_pattern_tree(target);

	//while (NULL != (tree = parse_tree())) {
		/* Keep a list of the original nodes. We're going to unlink
		 * some, which will then be "invisible" and can't be free()d
		 * the usual way. */
		struct llist *original_nodes_in_order =
			shallow_copy(tree->nodes_in_order);
		/* No nodes are free()d in here... */
		int match = process_tree(tree, pattern_labels, pattern_newick);
		/* Now we free the original nodes */
		struct list_elem *el = original_nodes_in_order->head;
		for (; NULL != el; el = el->next) {
			struct rnode *current = el->data;
			destroy_rnode(current, NULL);
		}
		destroy_llist(original_nodes_in_order);
		destroy_llist(tree->nodes_in_order);
		//free(tree);
	//}

	destroy_hash(pattern_labels);
	free(pattern_newick);
	destroy_tree_cb_2(pattern_tree, NULL);
	destroy_tree_cb(tree, NULL);
	return match;
}


int parse_labels(struct rooted_tree *tree, char label_list[1000][200])
{
	
	struct list_elem *elem;
	int first_line = 1;
        int i = 0;
	for (elem = tree->nodes_in_order->head; NULL != elem; elem = elem->next) 
	{
		struct rnode *current = (struct rnode *) elem->data;
		char *label = current->label;
		if (strcmp("", label) == 0)
			continue;
		if (is_leaf(current)) 
		{
			//printf("\nHello!!");
			//if (! first_line) putchar(' ');
			//printf ("%s", label);
			strcpy(label_list[i++],label);			
			//if (first_line) first_line = 0;			
		} 
		
	}
	return i;
	//putchar('\n');
	
}


struct llist *reverse_labels(struct rooted_tree *tree, struct llist *labels)
{
	struct llist *rev_labels = create_llist();
	/* We use a hash for looking up labels, instead of going over the list
	 * of labels every time (see below) */
	struct hash *labels_h = create_hash(labels->count);
	if (NULL == labels_h) {
		fprintf(stderr, "Memory error -exiting.\n");
		exit(EXIT_FAILURE);
	}

	struct list_elem *elem;

	/* fill label hash with the labels */
	char *PRESENT = "present";
	for (elem = labels->head; NULL != elem; elem = elem->next) {
		char *label = elem->data;
		if (! hash_set(labels_h, label, PRESENT)) {
			fprintf(stderr, "Memory error -exiting.\n");
			exit(EXIT_FAILURE);
		}
	}

	/* Now iterate over all nodes in the tree and see if their labels are
	 * found in the labels hash. If not, add them to the 'rev_labels' list.
	 * */
	for (elem=tree->nodes_in_order->head; NULL!=elem; elem=elem->next) {
		char *label = ((struct rnode*) elem->data)->label;
		if (0 == strcmp("", label)) continue;
		if (NULL == hash_get(labels_h, label)) {
			if (! append_element(rev_labels, label)) {
				fprintf(stderr, "Memory error -exiting.\n");
				exit(EXIT_FAILURE);
			}
		}
	}
	destroy_hash(labels_h);

	return rev_labels;
}

/* Create a linked list of labels from an array of char pointers */
struct llist* create_label_list(char argv[1000][200], int label_count)
{               
                struct llist *lbl_list = create_llist();
		if (NULL == lbl_list) { perror(NULL); exit(EXIT_FAILURE); }
		int i;
		for (i =0; i< label_count; i++) 
		{
			if (! append_element(lbl_list, argv[i])) 
			{
				perror(NULL);
				exit(EXIT_FAILURE);
			}
		}
		return lbl_list;
}

/* prune a patternparams from a tree, pass the pattern as a linked list of labels */
char* prune_tree(struct rooted_tree *tree, struct llist *labels)
{
	struct hash *lbl2node_map = create_label2node_map(tree->nodes_in_order);
	struct list_elem *elem;

	for (elem = labels->head; NULL != elem; elem = elem->next) {
		char *label = elem->data;
		struct rnode *goner = hash_get(lbl2node_map, label);
		if (NULL == goner) {
			fprintf (stderr, "WARNING: label '%s' not found.\n",
					label);
			continue;
		}
		/* parent may have been unlinked already, so let's check */
		if (NULL == goner->parent)
			continue;
		enum unlink_rnode_status result = unlink_rnode(goner);
		switch(result) {
		case UNLINK_RNODE_DONE:
			break;
		case UNLINK_RNODE_ROOT_CHILD:
			unlink_rnode_root_child->parent = NULL;
			tree->root = unlink_rnode_root_child;
			break;
		case UNLINK_RNODE_ERROR:
			fprintf (stderr, "Memory error - exiting.\n");
			exit(EXIT_FAILURE);
		default:
			assert(0); /* programmer error */
		}
	}

	destroy_hash(lbl2node_map);
	char* pruned_tree = to_newick(tree->root);
	return pruned_tree;
}

/* Overlap b/w 2 label sets */
int overlap_labels(char*  seed1, char* seed2)
{
	int i, j, overlap= 0;
	
	/*char** label_set1;
	label_set1 = (char**) malloc(sizeof(char*) * 1000);
	for(i=0; i<1000; i++)
	  {
	    label_set1[i] = (char*) malloc(sizeof(char) * LABEL_LENGTH);	    
	  }
	
	char** label_set2;
	label_set2 = (char**) malloc(sizeof(char*) * 1000);
	for(i=0; i<1000; i++)
	  {
	    label_set2[i] = (char*) malloc(sizeof(char) * LABEL_LENGTH);	    
	  }*/
	  
	char label_set1[1000][200];
	char label_set2[1000][200];
	int lc1, lc2;
	
		
	struct rooted_tree *seed1_tree = get_ordered_pattern_tree(seed1);
	struct rooted_tree *seed2_tree = get_ordered_pattern_tree(seed2);
	
	
	lc1 = parse_labels(seed1_tree, label_set1);
	lc2 = parse_labels(seed2_tree, label_set2);
		
	//printf("\nlc1 = %d lc2 = %d",lc1, lc2);
	for(i=0; i<lc1; i++)
	  {
	    for(j=0; j<lc2; j++)
	      {
	        //printf("\n%s",label_set2[j]);
	        if(strcmp(label_set1[i], label_set2[j]) == 0)
	          {
	            overlap++;
	            break;	            
	          }
	      }
	  }
        /*for(i=0; i<1000; i++)
	  {
	    free(label_set1[i]);
	    free(label_set2[i]);
	  }
	free(label_set1);
	free(label_set2);*/
	destroy_tree_cb_2(seed1_tree, NULL);
	destroy_tree_cb_2(seed2_tree, NULL);
	//free(seed1_tree);
	//free(seed2_tree);
	return overlap;
}


int main(int argc, char *argv[])
{
	char* tree2 ="(((a,b),(c,d)),e);";
	char* subtree2 = "((c,d),e);";
	//struct parameters params = get_params(tree2, subtree2);
	struct rooted_tree *tree = get_ordered_pattern_tree(tree2);
	
	int return_value = match_pattern(tree2,subtree2);
	
	//tree = get_ordered_pattern_tree(params.target_trees);
	//params = get_params(tree2, subtree2);	
	struct rooted_tree *subtree = get_ordered_pattern_tree(subtree2);
	
	char subtree_labels[1000][200];
	char tree_labels[1000][100];
	int label_count1, label_count2;
	label_count2 = parse_labels(subtree, subtree_labels);
	//label_count1 = parse_labels(tree, tree_labels);
	struct llist* subtree_ll = create_label_list(subtree_labels, label_count2);
	//struct llist* tree_ll = create_label_list(tree_labels, label_count2);
	struct llist *rev_labels = reverse_labels(tree,subtree_ll);
	
	char* pruned_tree = prune_tree(tree, rev_labels);
	
	//char* pruned_tree = prune_tree(tree2, subtree2);
	printf("\nPruned Tree = %s",pruned_tree);
	
	//printf("%d %d", label_count1, label_count2);	
	////int overlap = overlap_labels(tree2, subtree2);
	//printf("\nOverlap = %d \n",overlap);	
	//char* tree2 ="(((a,b),(c,d)),e);";
        //char* subtree2 = "((c,d),e);";
        //int present = match_pattern(tree2, subtree2);
        //printf("\nPresent is %d", present);
  	//if(argc!=4)
  	 {
  	   //printf("Number of arguments is wrong. Exiting.!");
  	   //exit(1);
  	 }
  	  
  	  
	//new_main(argc, argv);
	
	printf("\n");
	//free(tree);
	destroy_tree_cb_2(tree, NULL);
	//free(subtree);
	destroy_tree_cb_2(subtree, NULL);
	//free(subtree_ll);
	struct list_elem *el1 = subtree_ll->head;
		for (; NULL != el1; el1 = el1->next) {
			struct rnode *current = el1->data;
			destroy_rnode(current, NULL);
		}
	destroy_llist(subtree_ll);
	//free(rev_labels);
	struct list_elem *el2 = rev_labels->head;
		for (; NULL != el2; el2 = el2->next) {
			struct rnode *current = el2->data;
			destroy_rnode(current, NULL);
		}
	destroy_llist(rev_labels);	
	return 0;
}


int new_main(int argc, char* argv[])
{
  char  *seed_file = argv[1];
  char *tree_file = argv[2];
  char opfile[100];
  strcpy(opfile, seed_file);
  strcat(opfile, tree_file);
  strcat(opfile, "_OP");
  char freq_file[100];
  char freq[10]; 
  int frequency = atoi(argv[3]);
  printf("\nThe frequency entered is %d", frequency);
  char** seeds_array = malloc(1000 * sizeof (char *));
  char** trees_array = malloc(1000 * sizeof (char *));
  int freq_array[1000];
  int cutoff;
  int k;
  int i,j;
  //for(k =0; k<1000; k++)
   {
    // seeds_array[k] = (char*) malloc(sizeof (char) * 10000);
   }
  char* seed = (char*) malloc(30000);
  char* tree = (char*) malloc(50000);
  int seedcount =0;
  int treecount =0;
  int freqcount = 0;  
  
  if(argc!= 4)
    {
     printf("Insufficient Arguments ! Exiting! \n");
     exit(1);
    }	 
    
  FILE *fp;  
  printf("\nSeed file is %s",seed_file);
  fp=fopen(seed_file, "r");  
  if(fp == NULL )
    {perror("\nError opening seed file\n");exit(1);}
  else
   {
     
     while(fgets(seed, 30000, fp)!= NULL)
       {
         //printf("\n%s",seed); 
         seeds_array[seedcount] = (char*) malloc(sizeof (char) * 30000);        
         strcpy(seeds_array[seedcount++], seed);
       }
       
   }  
  fflush(fp);
  fclose(fp);
  
  printf("\nThe number of seeds is %d", seedcount);
  
  //for(j = 0; j<seedcount; j++)
    //{
      //printf("\n%s", seeds_array[j]);
    //}     
   
  printf("\nTree file is %s",tree_file);
  fp=fopen(tree_file, "r");  
  if(fp == NULL )
    {perror("\nError opening tree file\n");exit(1);}
  else
   {     
     while(fgets(tree, 50000, fp)!= NULL)
       {
         //printf("\n%s",tree); 
         trees_array[treecount] = (char*) malloc(sizeof (char) * 50000);        
         strcpy(trees_array[treecount++], tree);
       }       
   } 
  fflush(fp); 
  fclose(fp);
  
  printf("\nThe number of trees is %d", treecount);
  
  
  cutoff = (frequency * treecount) / 100;
  printf("\nThe cutoff is %d trees", cutoff);
  int false_cutoff = treecount - cutoff;
  printf("\nThe false cutoff is %d trees", false_cutoff);
  strcpy(freq_file, seed_file);
  strcat(freq_file, "_frequencies");
  
  printf("\nFrequency file is %s ",freq_file);
  fp=fopen(freq_file, "r");  
  if(fp == NULL )
    {perror("\nError opening freq file\n");exit(1);}
  else
   {
     
     while(fgets(freq, 5, fp)!= NULL)
       {
         //printf("\n%s",tree);
         int f = atoi(freq); 
         freq_array[freqcount++] = f;
       }
       
   }  
  fflush(fp);
  fclose(fp);  
  printf("\nThe number of frequencies is %d", freqcount); 
  
  
  int** seeds_trees;
  seeds_trees = (int**) malloc (sizeof(int*)* seedcount);
  for(i=0; i<seedcount; i++)
    {
      seeds_trees[i] = (int*) malloc(sizeof(int) * treecount);
    }
  
  
  
  
  for(i =0; i<seedcount; i++)
    {       //int present_seed =0;  
	    for(j=0; j<treecount; j++)
	      { 
	        
		int present = match_pattern(trees_array[j], seeds_array[i]);
		if(present == 1)
		  {
		    //printf("\npresent");
		    //present_seed++;
		    seeds_trees[i][j] = 1;          
		  }
		else
		  {
		    //printf("absent");
		    seeds_trees[i][j] = 0;          
		  }          
	      }
	      //printf("\npresent seed %d freqarray %d ",present_seed, freq_array[i]);
	      //printf("\n");
     }
  
  
  FILE *op;
  op=fopen(opfile, "w");
  int* MAST_present = (int*) malloc(sizeof(int) * treecount);   /* trees where the MAST is present */
  int* MAST_seed_present = (int*) malloc(sizeof(int) * treecount); /* trees where MAST and the current seed are present */  
  int* new_MAST_present = (int*) malloc(sizeof(int) * treecount); 
   
  char* MAST_temp = (char*) malloc(sizeof(char) * MAST_LENGTH); 
  for(i = 0; i<seedcount; i++)
   {
     //printf("\n-3");
     int l, MAST_presentno =0;
     int seed_translate[2000];//no of seeds.     
     for(l =0; l<seedcount; l++)
       seed_translate[l] = l;
     printf("\nOUTER SEED %d",i);
      for(l =0; l<treecount; l++)
       {
         if(seeds_trees[i][l] ==1)
           MAST_present[MAST_presentno++] = l;// the trees where seed 'i' is present.
       }
     strcpy(MAST_temp, seeds_array[i]);
     int max_overlap = 0;
     for(k =0; k<seedcount-1; k++)
     {
       //printf("\nConsidering seed number %d",k);
       int max_overlap = -1;
       int seed_addno = seed_translate[k];
       int j_ut = k;
       //printf("\n-2");
       for(j=k; j<seedcount; j++)
         {            
           int j_t = seed_translate[j];  
           if(i!=j_t)
           {
		   if(freq_array[j_t] == freq_array[k])
		     {		       
		       //printf("\nequal freq");
		       int temp_overlap = overlap_labels(MAST_temp, seeds_array[j_t]);
		       //printf("\nOverlap for %d = %d", j_t, temp_overlap);
		       if( temp_overlap >= max_overlap)
			{
			  max_overlap = temp_overlap;// find seed j_t with max overlap. 
			  seed_addno  = j_t;
			  j_ut = j;
			  //printf("\ngreatest j_ut %d",j_ut);
			}		   
		     }
		   else
		     {
			break;
		     }	   
	    }   
        }
       //printf("\n-7");
       //printf("\nMaxOverlap for %d = %d", seed_addno, max_overlap);
       /* translate indexes swapped */         
       //printf("\ninner seed number %d", seed_addno);
       int temp = seed_translate[j_ut];
       seed_translate[j_ut] = seed_translate[k];
       seed_translate[k] = temp;
       
       //for(l =0; l<seedcount; l++)
         //printf("\nseed_translate for %d is %d", l, seed_translate[l]);       
       //printf("\n-1");
       char* add_seed = seeds_array[seed_addno]; /* seed to be added to the MAST */
       int both_present = 0;/* both the MAST aprintf("\npruned MAST is %s",new_MAST);nd the current seed are present */
      
       for(l =0; l<MAST_presentno; l++)
        {
          int tree_index = MAST_present[l];
          if(seeds_trees[seed_addno][tree_index] == 1)
           {             
             MAST_seed_present[both_present++] = tree_index;
           }
        }
       //printf("\n0");  
       int tree_checked[400];
       int tc;
       for(tc =0; tc<treecount; tc++)
         tree_checked[tc] = 0;
       //printf("\n Both present is %d", both_present);
       if(both_present>cutoff)
        {
          //printf("\n1");  
          int false_freq = 0;
          for(l =0; l<both_present; l++)
            {
              int tree_id = MAST_seed_present[l];
              
              if(tree_checked[tree_id] == 0)
              {
		      //int tree_index = MAST_present_temp[l];
		      int MAST_lc, seed_lc;
		      char labels[1000][200];
		      char combined_labels[1000][200];
		      char* new_MAST =NULL;
		      //char* new_MAST  = (char*) malloc(sizeof(char) * MAST_LENGTH);
		      //char new_MAST[20000];
		      int mast_lcnt, seed_lcnt, combined_lc =0;
		      
		      //printf("\n\n Current MAST is %s", MAST_temp);
		      //printf("\n\n Current added seed is %s",  add_seed); 
		      //printf("\nCurrent Tree");
		      struct rooted_tree *current_tree = get_ordered_pattern_tree(trees_array[tree_id]);
		      //printf("\nMAST tree");
		      struct rooted_tree *MAST_tree = get_ordered_pattern_tree(MAST_temp);              
		      MAST_lc = parse_labels(MAST_tree, labels);  
		      
		      for(mast_lcnt=0; mast_lcnt<MAST_lc; mast_lcnt++)
		        strcpy(combined_labels[combined_lc++], labels[mast_lcnt]); 
		        
		      // struct llist* MAST_ll = create_label_list(labels, MAST_lc);  	      
		      /* remove labels in current tree && not in MAST */
		      //struct llist *rev_MAST_labels = reverse_labels(current_tree,MAST_ll);
		      //pruned_temp = prune_tree(current_tree, rev_MAST_labels);		      
		      //printf("\n\n\n\ttemp pruned MAST is %s",pruned_temp);
		      //struct rooted_tree *pruned_temp_tree = get_ordered_pattern_tree(pruned_temp);		      	      
		      
		      //printf("\nSeed tree");
		      struct rooted_tree *seed_tree        = get_ordered_pattern_tree(add_seed);    		                
		      seed_lc = parse_labels(seed_tree, labels);   
		      for(seed_lcnt=0; seed_lcnt<seed_lc; seed_lcnt++)
		        strcpy(combined_labels[combined_lc++], labels[seed_lcnt]);
		      struct llist* combined_ll = create_label_list(combined_labels, combined_lc); 
		      struct llist *rev_combined_labels = reverse_labels(current_tree,combined_ll);
		      new_MAST = prune_tree(current_tree, rev_combined_labels);
		      //free(current_tree);
		      destroy_tree_cb_2(current_tree, NULL);
		      //free(MAST_tree);
		      destroy_tree_cb_2(MAST_tree, NULL);
		      //free(seed_tree);
		      destroy_tree_cb_2(seed_tree, NULL);
		      //printf("\n2");  
		      
		      //printf("\n\tpruned MAST is %s",new_MAST);		         
		      //struct llist* seed_ll = create_label_list(labels, seed_lc);		      
		      /* remove labels in current tree && not in MAST */
		      //struct llist *rev_seed_labels = reverse_labels(pruned_temp_tree,seed_ll);		      
		      //new_MAST = prune_tree(pruned_temp_tree, rev_seed_labels);
		      //printf("\n\tpruned MAST is %s",new_MAST);
		      free(combined_ll);
		      free(rev_combined_labels);
		      int newmast_present_count = 0, p;
		      //printf("both present is %d",both_present);
		      for(p =0; p<both_present; p++)
		        {
		          int tree_id = MAST_seed_present[p];
		          //printf("tree id is %d",tree_id);		          
		          if(tree_checked[tree_id] == 0)
		            { 		              
		              int present = match_pattern(trees_array[tree_id], new_MAST);
			      if(present == 1)
			        {
			         new_MAST_present[newmast_present_count++] = tree_id;			
			         //printf("\n\n PRESENT tree id is %d",tree_id);
			         tree_checked[tree_id] = 1;
			        }
			    }
			}//for p
			//printf("\n3");  
			//printf("\nCombined_cutoff is %d",combined_cutoff);
		        if(newmast_present_count > cutoff)
			  {
			    printf("\nAdded seed %d frequency = %d", seed_addno, newmast_present_count);
			    strcpy(MAST_temp, new_MAST);
			    MAST_presentno = newmast_present_count;
			    MAST_present = new_MAST_present;
			    //printf("\nMAST_temp = %s MAST_presentno = %d", MAST_temp, MAST_presentno);
			    break;
			  }
			else 
			  {
			    false_freq += newmast_present_count;
			    if(false_freq > false_cutoff)
			    {
			     //printf("\tcombined not added seed %d false freq = %d", seed_addno, false_freq);
			     break;
			    }			      
			  }  
			  //printf("\n4");
			//free(new_MAST);   
			
			free(new_MAST);
			
	      }//tree_checked = 0
	      
            }//for l 0: both_present
          }//both present >cutoff  
          //else{printf("\tboth present failed.");}   
      }//for inner seed   
      fputs(MAST_temp, op);
      printf("\nMAST_temp = %s MAST_presentno = %d", MAST_temp, MAST_presentno);
      fputs("\n", op);      
          
   }// for outer seed
 //printf("\n");
 free(MAST_temp);  
 free(MAST_present); 
 free(MAST_seed_present);
 free(new_MAST_present);
 fflush(op);
 fclose(op);
 
 for(i=0; i<seedcount; i++)
   {
     free(seeds_array[i]);    
   }
 free(seeds_array);
 
 for(i=0; i<treecount; i++)
   {
      free(trees_array[i]);    
   }
  free(trees_array);
  for(i=0; i<seedcount; i++)
    {
      free(seeds_trees[i]);
    }
  free(seeds_trees); 
  free(seed);
  free(tree);
 
}//main fn

