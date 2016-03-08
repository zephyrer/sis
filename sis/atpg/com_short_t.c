/*
 * Revision Control Information
 *
 * $Source: /users/pchong/CVS/sis/sis/atpg/com_short_t.c,v $
 * $Author: pchong $
 * $Revision: 1.1.1.1 $
 * $Date: 2004/02/07 10:14:17 $
 *
 */
#include <setjmp.h>
#include <signal.h>
#include "sis.h"
#include "atpg_int.h"

static void print_usage();
static int set_short_tests_options();
static void update_start_states();
static void update_product_start_states();

static jmp_buf timeout_env;
static void timeout_handle()
{
  longjmp(timeout_env, 1);
}

static void
print_usage()
{
    fprintf(sisout, "usage: short_tests [-fFhirtvV] [fil\n");
    fprintf(sisout, "-f\tno fault simulation\n");
    fprintf(sisout, "-F\tno reverse fault simulation\n");
    fprintf(sisout, "-h\tuse fast SAT; no non-local implications\n"); 
    fprintf(sisout, "-i\tdo not use internal states as start states for tests\n"); 
    fprintf(sisout, "-r\tuse random test generation and propagation\n");
    fprintf(sisout, "-t\tperform tech decomp of network\n"); 
    fprintf(sisout, "-v\tverbosity\n");
    fprintf(sisout, "-V\tall tests generated by product machine traversal\n");
    fprintf(sisout, "file\toutput file for test patterns\n");
}

static int
set_short_tests_options(info, argc, argv)
atpg_info_t *info;
int argc;
char **argv;
{
    atpg_options_t *atpg_opt;
    int c;
    char *fname, *real_filename;

    atpg_opt = info->atpg_opt;

    /* default values */
    atpg_opt->quick_redund = FALSE;
    atpg_opt->reverse_fault_sim = TRUE;
    atpg_opt->PMT_only = FALSE;
    atpg_opt->use_internal_states = TRUE;
    atpg_opt->n_sim_sequences = WORD_LENGTH;
    atpg_opt->deterministic_prop = TRUE;
    atpg_opt->random_prop = FALSE;
    atpg_opt->rtg_depth = -1;
    atpg_opt->fault_simulate = TRUE;
    atpg_opt->fast_sat = FALSE;
    atpg_opt->rtg = FALSE;
    atpg_opt->build_product_machines = TRUE;
    atpg_opt->tech_decomp = FALSE;
    atpg_opt->timeout = 0;
    atpg_opt->verbosity = 0;
    atpg_opt->prop_rtg_depth = 10;
    atpg_opt->n_random_prop_iter = 1;
    atpg_opt->print_sequences = FALSE;

    util_getopt_reset();
    while ((c = util_getopt(argc, argv, "DfFhirtT:v:V")) != EOF) {
	switch (c) {
	    case 'D':
		atpg_opt->deterministic_prop = FALSE;
		break;
	    case 'f':
		atpg_opt->fault_simulate = FALSE;
		break;
	    case 'F':
		atpg_opt->reverse_fault_sim = FALSE;
		break;
	    case 'h':
		atpg_opt->fast_sat = TRUE;
		break;
	    case 'i':
		atpg_opt->use_internal_states = FALSE;
		break;
	    case 'r':
		atpg_opt->rtg = TRUE;
		atpg_opt->random_prop = TRUE;
		break;
	    case 't':
		atpg_opt->tech_decomp = TRUE;
		break;
	    case 'T':
		atpg_opt->timeout = atoi(util_optarg);
		if (atpg_opt->timeout < 0 || atpg_opt->timeout > 3600 * 24 * 365) {
		    return 0;
		}
		break;
	    case 'v':
		atpg_opt->verbosity = atoi(util_optarg);
		break;
	    case 'V':
		atpg_opt->PMT_only = TRUE;
		break;
	    default:
		return 0;
	}
    }
    if (argc - util_optind == 1) {
	atpg_opt->print_sequences = TRUE;
	fname = argv[util_optind];
	atpg_opt->fp = com_open_file(fname, "w", &real_filename, /* silent */ 0);
	atpg_opt->real_filename = real_filename;
	if (atpg_opt->fp == NULL) {
	    FREE(real_filename);
	    return 0;
	}
    }
    else if (argc - util_optind != 0) {
	return 0;
    }
    return 1;	/* everything's OK */
}

int
com_short_tests(network, argc, argv)
network_t **network;
int argc;
char **argv;
{
    long begin_time, time, last_time;
    int stg_depth, n_tests;
    atpg_info_t *info;
    atpg_ss_info_t *ss_info;
    seq_info_t *seq_info;
    fault_t *fault;
    lsHandle handle;
    lsGeneric data;
    atpg_options_t *atpg_opt;
    sequence_t *test_sequence;
    lsList covered, testable_faults;
    sat_result_t sat_value;
    fault_pattern_t *fault_info;
    bool use_internal_states = FALSE;

    if ((*network) == NIL(network_t)) return 0;
    if (network_num_internal(*network) == 0) return 0;
    if (network_num_latch(*network) == 0) {
	fprintf(sisout, "Use the atpg command for combinational circuits.\nThe short_tests techniques are useful only for sequential circuits.\n");
	return 0;
    }

    last_time = begin_time = util_cpu_time();
    info = atpg_info_init(*network);
    info->seq = TRUE;
    atpg_opt = info->atpg_opt;
    if (!set_short_tests_options(info, argc, argv)) {
	print_usage();
	atpg_free_info(info);
	return 1;
    }
    if (atpg_opt->timeout > 0) {
    	(void) signal(SIGALRM, timeout_handle);
    	(void) alarm((unsigned int) atpg_opt->timeout);
    	if (setjmp(timeout_env) > 0) {
	    fprintf(sisout, "timeout occurred after %d seconds\n", 
		    atpg_opt->timeout);
	    atpg_free_info(info);
	    return 1;
    	}
    }
    if (atpg_opt->tech_decomp) {
	decomp_tech_network(*network, INFINITY, INFINITY);
    }

    ss_info = atpg_sim_sat_info_init(*network, info);
    seq_info = atpg_seq_info_init(info);
    atpg_gen_faults(info);
    atpg_sim_setup(ss_info);
    atpg_comb_sim_setup(ss_info);
    atpg_sat_init(ss_info);

    info->statistics->initial_faults = lsLength(info->faults);
    if (atpg_opt->verbosity > 0) {
	fprintf(sisout, "%d total faults\n", lsLength(info->faults));
    }

    seq_setup(seq_info, info);
    if (atpg_opt->build_product_machines) {
	seq_product_setup(info, seq_info, info->network);
	copy_orig_bdds(seq_info);
	atpg_product_setup_seq_info(seq_info);
    }
    record_reset_state(seq_info, info);

    time = util_cpu_time();
    info->time_info->setup = (time - last_time);
    last_time = time;

    assert(calculate_reachable_states(seq_info));
    seq_info->valid_states_network = convert_bdd_to_network(seq_info, 
				    seq_info->range_data->total_set);
    time = util_cpu_time();
    info->time_info->traverse_stg = (time - last_time);
    last_time = time;
    stg_depth = array_n(seq_info->reached_sets);
    info->statistics->stg_depth = stg_depth;
    atpg_sat_node_info_setup(seq_info->valid_states_network);
    atpg_setup_seq_info(info, seq_info, stg_depth);

    n_tests = 0;
    if (atpg_opt->rtg) {
	atpg_opt->rtg_depth = stg_depth;
	info->tested_faults = atpg_random_cover(info, ss_info, NIL(atpg_ss_info_t), 
						TRUE, &n_tests);
	info->statistics->n_RTG_tested = lsLength(info->tested_faults);
    } else {
	info->tested_faults = lsCreate();
    }
    if (atpg_opt->verbosity > 0) {
	fprintf(sisout, "%d faults covered by RTG\n", 
					lsLength(info->tested_faults));
    }
    time = util_cpu_time();
    info->time_info->RTG += (time - last_time);
    last_time = time;

    if (atpg_opt->PMT_only) {
	/* Before going to PMT, first find redundancies that can be identified 
	 * by SAT.  To cut down on SAT time, if RTG has not already been 
	 * performed, first run RTG to eliminate 
	 * testable faults from the search.  (These random tests are NOT used as 
	 * tests--tests will be generated later during PMT.)
	 */
	if (!atpg_opt->rtg) {
	    last_time = util_cpu_time();
	    atpg_opt->rtg_depth = stg_depth;
	    testable_faults = atpg_random_cover(info, ss_info, NIL(atpg_ss_info_t), 
						FALSE, NIL(int));
	    time = util_cpu_time();
	    info->time_info->RTG += (time - last_time);
	    concat_lists(info->untested_faults, testable_faults);
	}

	while (lsFirstItem(info->faults, (lsGeneric *) &fault, &handle) == LS_OK) {
	    if (atpg_opt->verbosity > 0) {
		atpg_print_fault(sisout, fault);
	    }
	    last_time = util_cpu_time();
	    sat_reset(ss_info->atpg_sat);
	    (void) atpg_network_fault_clauses(ss_info, NIL(atpg_ss_info_t), 
						   seq_info, fault);
	    time = util_cpu_time();
	    info->time_info->SAT_clauses += (time - last_time);
	    last_time = time;

	    sat_value = sat_solve(ss_info->atpg_sat, atpg_opt->fast_sat, 
				  atpg_opt->verbosity);
	    time = util_cpu_time();
	    info->time_info->SAT_solve += (time - last_time);
	    if (sat_value == SAT_ABSURD) {
		if (atpg_opt->verbosity > 0) {
		    fprintf(sisout, "Redundant\n");
		}
		fault->status = REDUNDANT;
		fault->redund_type = CONTROL;
		info->statistics->sat_red += 1;
		lsNewEnd(info->redundant_faults, (lsGeneric) fault, 0);
		if (atpg_opt->reverse_fault_sim) {
		    fault_info = ALLOC(fault_pattern_t, 1);
    		    fault_info->node = fault->node;
    		    fault_info->fanin = fault->fanin;
    		    fault_info->value = (fault->value == S_A_0) ? 0 : 1;
		    st_insert(info->redund_table, (char *) fault_info, (char *) 0);
		}
		lsRemoveItem(handle, (lsGeneric *) &data);
	    } else {
		lsNewEnd(info->untested_faults, (lsGeneric) fault, 0);
		lsRemoveItem(handle, &data);
	    }
	}

    } else {
	while (lsFirstItem(info->faults, (lsGeneric *) &fault, &handle) == LS_OK) {
	    if (atpg_opt->verbosity > 0) {
		atpg_print_fault(sisout, fault);
	    }
	    /* find test using three-step algorithm and fault-free assumption */
	    test_sequence = generate_test(fault, info, ss_info, seq_info, 
					  NIL(atpg_ss_info_t), 0);
     
	    switch (fault->status) {
		case REDUNDANT:
		    if (atpg_opt->verbosity > 0) {
			fprintf(sisout, "Redundant\n");
		    }
		    lsNewEnd(info->redundant_faults, (lsGeneric) fault, 0);
		    if (atpg_opt->reverse_fault_sim) {
		    	fault_info = ALLOC(fault_pattern_t, 1);
    		    	fault_info->node = fault->node;
    		    	fault_info->fanin = fault->fanin;
    		    	fault_info->value = (fault->value == S_A_0) ? 0 : 1;
		    	st_insert(info->redund_table, (char *) fault_info, (char *) 0);
		    }
		    lsRemoveItem(handle, (lsGeneric *) &data);
		    break;
		case ABORTED:
		    if (atpg_opt->verbosity > 0) {
			fprintf(sisout, "Aborted by SAT\n");
		    }
		    lsNewEnd(info->untested_faults, (lsGeneric) fault, 0);
		    lsRemoveItem(handle, &data);
		    break;
		case UNTESTED:
		    if (atpg_opt->verbosity > 0) {
			fprintf(sisout, "Untested\n");
		    }
		    lsNewEnd(info->untested_faults, (lsGeneric) fault, 0);
		    lsRemoveItem(handle, &data);
		    break;
		case TESTED:
		    n_tests ++;
		    if (atpg_opt->verbosity > 2) {
	    		atpg_print_vectors(sisout, test_sequence->vectors, 
					   info->n_real_pi);
		    }
		    if (ATPG_DEBUG) {
			assert(atpg_verify_test(ss_info, fault, test_sequence));
		    }
		    if (atpg_opt->verbosity > 0) {
			fprintf(sisout, "Tested\n");
		    }
		    lsNewEnd(info->tested_faults, (lsGeneric) fault, 0);
		    lsRemoveItem(handle, &data);
		    test_sequence->index = n_tests;
		    fault->sequence = test_sequence;
		    st_insert(info->sequence_table, (char *) test_sequence, (char *) 0);
		    if (atpg_opt->use_internal_states) {
		    	update_start_states(info, seq_info, test_sequence, 
					    ss_info);
		    }
		    if (atpg_opt->fault_simulate) {
			last_time = util_cpu_time();
		    	covered = seq_single_sequence_simulate(ss_info, 
					test_sequence, info->faults);
			concat_lists(covered, 
				     seq_single_sequence_simulate(ss_info, 
					    test_sequence, info->untested_faults));
    			if (info->atpg_opt->verbosity > 0) {
			    fprintf(sisout, "fault simulation covered %d\n", 
				    lsLength(covered));
    			}
			concat_lists(info->tested_faults, covered);
			time = util_cpu_time();
			info->time_info->fault_simulate += (time - last_time);
		    }
		    break;
		default: 
		    fail("bad fault->status returned by generate_test");
		    break;
	    }
	    if (atpg_opt->verbosity > 0) {
		fprintf(sisout, "%d faults remaining\n", 
			lsLength(info->faults) + lsLength(info->untested_faults));
	    }
	}
    }

    info->statistics->n_untested_by_main_loop = lsLength(info->untested_faults);

    if (atpg_opt->build_product_machines && 
				(info->statistics->n_untested_by_main_loop > 0)) {
	if (atpg_opt->use_internal_states) {
	    construct_product_start_states(seq_info, info->n_latch);
	}
    	while (lsFirstItem(info->untested_faults, 
					(lsGeneric *) &fault, &handle) == LS_OK) {
	    if (atpg_opt->verbosity > 0) {
	    	atpg_print_fault(sisout, fault);
	    }
	    /* find test using good/faulty product machine traversal */
	    last_time = util_cpu_time();
	    test_sequence = generate_test_using_verification(fault, info,
						ss_info, seq_info, 0);
	    time = util_cpu_time();
	    info->time_info->product_machine_verify += (time - last_time);
	    switch (fault->status) {
	    	case REDUNDANT:
                    if (atpg_opt->verbosity > 0) {
                    	fprintf(sisout, "Redundant\n");
                    }
		    lsNewEnd(info->redundant_faults, (lsGeneric) fault, 0);
		    if (atpg_opt->reverse_fault_sim) {
		    	fault_info = ALLOC(fault_pattern_t, 1);
    		    	fault_info->node = fault->node;
    		    	fault_info->fanin = fault->fanin;
    		    	fault_info->value = (fault->value == S_A_0) ? 0 : 1;
		    	st_insert(info->redund_table, (char *) fault_info, (char *) 0);
		    }
		    lsRemoveItem(handle, &data);
		    break;
	    	case TESTED:
		    n_tests ++;
		    if (atpg_opt->verbosity > 2) {
	    		atpg_print_vectors(sisout, test_sequence->vectors, 
					   info->n_real_pi);
		    }
		    if (ATPG_DEBUG) {
		    	assert(atpg_verify_test(ss_info, fault, test_sequence));
		    }
                    if (atpg_opt->verbosity > 0) {
                    	fprintf(sisout, "Tested\n");
                    }
		    lsNewEnd(info->tested_faults, (lsGeneric) fault, 0);
		    lsRemoveItem(handle, &data);
		    test_sequence->index = n_tests;
		    fault->sequence = test_sequence;
		    st_insert(info->sequence_table, (char *) test_sequence, 
			      (char *) 0);
		    if (atpg_opt->use_internal_states) {
		    	update_product_start_states(info, seq_info, test_sequence, 
						    ss_info);
		    }
		    if (atpg_opt->fault_simulate) {
		    	last_time = util_cpu_time();
		    	covered = seq_single_sequence_simulate(ss_info, 
					test_sequence, info->untested_faults);
    			if (info->atpg_opt->verbosity > 0) {
			    fprintf(sisout, "fault simulation covered %d\n", 
				    lsLength(covered));
    			}
			concat_lists(info->tested_faults, covered);
		    	time = util_cpu_time();
    		    	info->time_info->fault_simulate += (time - last_time);
		    }
		    break;
	    	case UNTESTED:
		    assert(atpg_opt->use_internal_states == TRUE);
                    if (atpg_opt->verbosity > 0) {
                    	fprintf(sisout, "Untested\n");
                    }
	    	    lsNewEnd(info->final_untested_faults, (lsGeneric) fault, 0);
	    	    lsRemoveItem(handle, &data);
		    break;
	    	default:
		    fail("bad fault->status returned by PMT");
		    break;
	    }
	    if (atpg_opt->verbosity > 0) {
	    	fprintf(sisout, "%d faults remaining\n", 
	lsLength(info->untested_faults) + lsLength(info->final_untested_faults));
	    }
    	}
	/* if internal states were used, PMT might not have generated a test 
	   for some faults */
	if (lsLength(info->final_untested_faults) > 0) {
	    assert(atpg_opt->use_internal_states == TRUE);
	    use_internal_states = TRUE;
	    atpg_opt->use_internal_states = FALSE;
	}
    	while (lsFirstItem(info->final_untested_faults, 
					(lsGeneric *) &fault, &handle) == LS_OK) {
	    if (atpg_opt->verbosity > 0) {
	    	atpg_print_fault(sisout, fault);
	    }
	    /* find test using good/faulty product machine traversal */
	    last_time = util_cpu_time();
	    test_sequence = generate_test_using_verification(fault, info,
						ss_info, seq_info, 0);
	    time = util_cpu_time();
	    info->time_info->product_machine_verify += (time - last_time);
	    switch (fault->status) {
	    	case REDUNDANT:
                    if (atpg_opt->verbosity > 0) {
                    	fprintf(sisout, "Redundant\n");
                    }
		    lsNewEnd(info->redundant_faults, (lsGeneric) fault, 0);
		    if (atpg_opt->reverse_fault_sim) {
		    	fault_info = ALLOC(fault_pattern_t, 1);
    		    	fault_info->node = fault->node;
    		    	fault_info->fanin = fault->fanin;
    		    	fault_info->value = (fault->value == S_A_0) ? 0 : 1;
		    	st_insert(info->redund_table, (char *) fault_info, (char *) 0);
		    }
		    lsRemoveItem(handle, &data);
		    break;
	    	case TESTED:
		    n_tests ++;
		    if (atpg_opt->verbosity > 2) {
	    		atpg_print_vectors(sisout, test_sequence->vectors, 
					   info->n_real_pi);
		    }
		    if (ATPG_DEBUG) {
		    	assert(atpg_verify_test(ss_info, fault, test_sequence));
		    }
                    if (atpg_opt->verbosity > 0) {
                    	fprintf(sisout, "Tested\n");
                    }
		    lsNewEnd(info->tested_faults, (lsGeneric) fault, 0);
		    lsRemoveItem(handle, &data);
		    test_sequence->index = n_tests;
		    fault->sequence = test_sequence;
		    st_insert(info->sequence_table, (char *) test_sequence, 
			      (char *) 0);
		    if (atpg_opt->use_internal_states) {
		    	update_product_start_states(info, seq_info, test_sequence, 
						    ss_info);
		    }
		    if (atpg_opt->fault_simulate) {
		    	last_time = util_cpu_time();
		    	covered = seq_single_sequence_simulate(ss_info, 
					test_sequence, info->untested_faults);
    			if (info->atpg_opt->verbosity > 0) {
			    fprintf(sisout, "fault simulation covered %d\n", 
				    lsLength(covered));
    			}
			concat_lists(info->tested_faults, covered);
		    	time = util_cpu_time();
    		    	info->time_info->fault_simulate += (time - last_time);
		    }
		    break;
	    	default:
		    fail("bad fault->status returned by PMT");
		    break;
	    }
	    if (atpg_opt->verbosity > 0) {
	    	fprintf(sisout, "%d faults remaining\n",  
			lsLength(info->final_untested_faults));
	    }
    	}
	if (use_internal_states == TRUE) atpg_opt->use_internal_states = TRUE;
    }

    if (atpg_opt->reverse_fault_sim) {
	reverse_fault_simulate(info, ss_info);
    }
    print_and_destroy_sequences(info);
    info->time_info->total_time = (util_cpu_time() - begin_time);
    atpg_print_results(info, seq_info);

    atpg_sim_unsetup(ss_info);
    atpg_comb_sim_unsetup(ss_info);
    atpg_sim_free(ss_info);
    atpg_sat_free(ss_info);
    FREE(ss_info);
    if (atpg_opt->build_product_machines) {
	seq_info_product_free(seq_info);
    }
    seq_info_free(info, seq_info);
    lsDestroy(info->tested_faults, free_fault);
    lsDestroy(info->faults, 0);
    atpg_free_info(info);
    sm_cleanup();
    fast_avl_cleanup();
    return 0;
}

static void
update_start_states(info, seq_info, test_sequence, ss_info)
atpg_info_t *info;
seq_info_t *seq_info;
sequence_t *test_sequence;
atpg_ss_info_t *ss_info;
{
    int i, key, value, position_in_key;
    int n_latch = info->n_latch;
    array_t *latch_to_pi_ordering = seq_info->latch_to_pi_ordering;
    array_t *good_state = seq_info->good_state;
    bool found;
    st_table *state_sequence_table = seq_info->state_sequence_table;
    lsList sequence_list;
    bdd_t *new_state, *new_start_states;

    fault_simulate_to_get_final_state(ss_info, test_sequence, seq_info);
    key = 0;
    for (i = n_latch; i --; ) {
	value = array_fetch(int, good_state, i);
	position_in_key = array_fetch(int, latch_to_pi_ordering, i);
	key += value << position_in_key;
    }

    found = st_lookup(state_sequence_table, (char *) key, (char **) &sequence_list);
    if (found) {
    	if (lsLength(sequence_list) == 0) {
	    /* final_state == reset_state */
	    return;
    	} else {
	    lsNewBegin(sequence_list, (lsGeneric) test_sequence, 0);
	}
    } else {
	sequence_list = lsCreate();
	lsNewBegin(sequence_list, (lsGeneric) test_sequence, 0);
	st_insert(state_sequence_table, (char *) key, (char *) sequence_list);
	new_state = convert_state_to_bdd(seq_info, good_state);
	new_start_states = bdd_or(seq_info->start_states, new_state, 1, 1);
	bdd_free(new_state);
	bdd_free(seq_info->start_states);
	seq_info->start_states = new_start_states;
    }
    return;
}

static void
update_product_start_states(info, seq_info, test_sequence, ss_info)
atpg_info_t *info;
seq_info_t *seq_info;
sequence_t *test_sequence;
atpg_ss_info_t *ss_info;
{
    int i, key, value, position_in_key;
    int n_latch = info->n_latch;
    array_t *latch_to_pi_ordering = seq_info->latch_to_pi_ordering;
    array_t *good_state = seq_info->good_state;
    bool found;
    st_table *state_sequence_table = seq_info->state_sequence_table;
    lsList sequence_list;
    bdd_t *new_state, *new_start_states;

    fault_simulate_to_get_final_state(ss_info, test_sequence, seq_info);
    key = 0;
    for (i = n_latch; i --; ) {
	value = array_fetch(int, good_state, i);
	position_in_key = array_fetch(int, latch_to_pi_ordering, i);
	key += value << position_in_key;
    }

    found = st_lookup(state_sequence_table, (char *) key, (char **) &sequence_list);
    if (found) {
    	if (lsLength(sequence_list) == 0) {
	    /* final_state == reset_state */
	    return;
    	} else {
	    lsNewBegin(sequence_list, (lsGeneric) test_sequence, 0);
	}
    } else {
	sequence_list = lsCreate();
	lsNewBegin(sequence_list, (lsGeneric) test_sequence, 0);
	st_insert(state_sequence_table, (char *) key, (char *) sequence_list);
	new_state = convert_states_to_product_bdd(seq_info, good_state, good_state);
	new_start_states = bdd_or(seq_info->product_start_states, new_state, 1, 1);
	bdd_free(new_state);
	bdd_free(seq_info->product_start_states);
	seq_info->product_start_states = new_start_states;
    }
    return;
}
