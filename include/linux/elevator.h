#ifndef _LINUX_ELEVATOR_H
#define _LINUX_ELEVATOR_H

typedef int (elevator_merge_fn) (request_queue_t *, struct list_head **,
				 struct bio *);

typedef void (elevator_merge_req_fn) (request_queue_t *, struct request *, struct request *);

typedef void (elevator_merged_fn) (request_queue_t *, struct request *);

typedef struct request *(elevator_next_req_fn) (request_queue_t *);

typedef void (elevator_add_req_fn) (request_queue_t *, struct request *, struct list_head *);
typedef int (elevator_queue_empty_fn) (request_queue_t *);
typedef void (elevator_remove_req_fn) (request_queue_t *, struct request *);
typedef struct request *(elevator_request_list_fn) (request_queue_t *, struct request *);
typedef struct list_head *(elevator_get_sort_head_fn) (request_queue_t *, struct request *);

typedef int (elevator_init_fn) (request_queue_t *, elevator_t *);
typedef void (elevator_exit_fn) (request_queue_t *, elevator_t *);

struct elevator_s
{
	elevator_merge_fn *elevator_merge_fn;
	elevator_merged_fn *elevator_merged_fn;
	elevator_merge_req_fn *elevator_merge_req_fn;

	elevator_next_req_fn *elevator_next_req_fn;
	elevator_add_req_fn *elevator_add_req_fn;
	elevator_remove_req_fn *elevator_remove_req_fn;

	elevator_queue_empty_fn *elevator_queue_empty_fn;

	elevator_request_list_fn *elevator_former_req_fn;
	elevator_request_list_fn *elevator_latter_req_fn;

	elevator_init_fn *elevator_init_fn;
	elevator_exit_fn *elevator_exit_fn;

	void *elevator_data;

	struct kobject kobj;
	struct kobj_type *elevator_ktype;
};

/*
 * block elevator interface
 */
extern void elv_add_request(request_queue_t *, struct request *, int, int);
extern void __elv_add_request(request_queue_t *, struct request *, int, int);
extern int elv_merge(request_queue_t *, struct list_head **, struct bio *);
extern void elv_merge_requests(request_queue_t *, struct request *,
			       struct request *);
extern void elv_merged_request(request_queue_t *, struct request *);
extern void elv_remove_request(request_queue_t *, struct request *);
extern int elv_queue_empty(request_queue_t *);
extern struct request *elv_former_request(request_queue_t *, struct request *);
extern struct request *elv_latter_request(request_queue_t *, struct request *);
extern int elv_register_queue(struct gendisk *);
extern void elv_unregister_queue(struct gendisk *);

#define __elv_add_request_pos(q, rq, pos)	\
	(q)->elevator.elevator_add_req_fn((q), (rq), (pos))

/*
 * noop I/O scheduler. always merges, always inserts new request at tail
 */
extern elevator_t elevator_noop;

/*
 * deadline i/o scheduler. uses request time outs to prevent indefinite
 * starvation
 */
extern elevator_t iosched_deadline;

extern int elevator_init(request_queue_t *, elevator_t *);
extern void elevator_exit(request_queue_t *);
extern inline int bio_rq_in_between(struct bio *, struct request *, struct list_head *);
extern inline int elv_rq_merge_ok(struct request *, struct bio *);
extern inline int elv_try_merge(struct request *, struct bio *);
extern inline int elv_try_last_merge(request_queue_t *, struct bio *);

/*
 * Return values from elevator merger
 */
#define ELEVATOR_NO_MERGE	0
#define ELEVATOR_FRONT_MERGE	1
#define ELEVATOR_BACK_MERGE	2

#endif
