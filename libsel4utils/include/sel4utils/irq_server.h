/*
 * Copyright 2017, Data61
 * Commonwealth Scientific and Industrial Research Organisation (CSIRO)
 * ABN 41 687 119 230.
 *
 * This software may be distributed and modified according to the terms of
 * the BSD 2-Clause license. Note that NO WARRANTY is provided.
 * See "LICENSE_BSD2.txt" for details.
 *
 * @TAG(DATA61_BSD)
 */

/**
 * The IRQ server helps to manage the IRQs in a system. There are 3 API levels
 * in the design
 *   1. [irq server node]   The IRQ server node is a set of IRQs and their
 *                          associated handlers. It is a passive system, hence,
 *                          the application is responsible for waiting on the
 *                          appropriate notification endpoint for events.
 *                          The AEP binding feature of the seL4 kernel may be used
 *                          here to listen to both synchronous IPC and IRQ
 *                          notification events.
 *   2. [irq server thread] An IRQ server node is a standalone thread which waits
 *                          for the arrival of any IRQ that is being managed by a
 *                          particular irq server node. When an IRQ is received,
 *                          the irq server thread will either forward the irq
 *                          information to a registered synchronous endpoint, or
 *                          call the appropriate handler function directly.
 *   3. [irq server]        A dynamic collection of server threads. When
 *                          registering an IRQ call back function, the irq server
 *                          will attempt to deligate the IRQ to an irq server node
 *                          that has not yet reached capacity. If no node can accept
 *                          the IRQ, a new irq server thread will be created to
 *                          support the additional demand.
 *
 * ++ Special notes ++
 *
 * Performance
 *   The application can achieve greater performance by configuring the irq server,
 *   or irq server threads, to call the IRQ handler functions directly. In this
 *   case, the application must take care to ensure that all concurrency issues are
 *   addressed.
 *
 * Resource availability
 *   The irq server API family accept resource allocators as arguments to some
 *   function calls. In a dynamic system design, these resource allocators
 *   must be kept available indefinitely, or until the system reaches a known
 *   steady state.
 */

#pragma once

#include <autoconf.h>

#include <sel4/sel4.h>
#include <vspace/vspace.h>
#include <vka/vka.h>
#include <simple/simple.h>
#include <platsupport/io.h>
#include <platsupport/irq.h>
#include <sel4platsupport/irq.h>

typedef struct irq_server irq_server_t;

typedef int thread_id_t;

/**
 * Allows a client to acknowledge an IRQ
 * @param[in] irq  The IRQ to acknowledge
 */
void irq_data_ack_irq(struct irq_data* irq);

/*********************************
 *** IRQ server node functions ***
 *********************************/

/**
 * Create a new IRQ server node.
 * @param[in] notification        An notification object that can be used for binding IRQ notification
 * @param[in] badge_mask A mask for the available badge. Bits NOT set in the mask are
 *                       considered reserved and will not be used for IRQ identification.
 *                       This does, however, reduce the number of IRQs that this node can
 *                       manage. One may choose to reserve badge bits for the
 *                       identification of other notifications to the same endpoint.
 * @return               A handle to the created IRQ server node, or NULL on failure.
 */
irq_server_node_t irq_server_node_new(seL4_CPtr notification, seL4_Word badge_mask);

/**
 * Register an IRQ with a server node.
 * @param[in] n            The irq server node to register the IRQ with.
 * @param[in] irq          The IRQ number of the IRQ to be registered
 * @param[in] cb           A callback function to be called when the IRQ event occurs
 * @param[in] token        A token to be passed, unmodified, to te callback function
 * @param[in] vka          An allocator for for kernel objects
 * @param[in] cspace       The current capability space
 * @param[in] simple       A simple interface for creating the irq handler
 * @return                 A handle to the irq structure that was created.
 */
struct irq_data* irq_server_node_register_irq(irq_server_node_t n, irq_t irq,
                                              irq_handler_fn cb, void* token,
                                              vka_t* vka, seL4_CPtr cspace,
                                              simple_t* simple);

/****************************
 *** IRQ server functions ***
 ****************************/

/**
 * Initialises an IRQ server.
 * The server will spawn threads to handle incoming IRQs. The function of the
 * threads is to IPC the provided synchronous endpoint with IRQ information. When the IPC
 * arrives, the application should call \ref{irq_server_handle_irq_ipc} while IPC
 * registers are still valid. \ref{irq_server_handle_irq_ipc} will decode the provided
 * information and redirect control to the appropriate IRQ handler.
 * @param[in] vspace       The current vspace
 * @param[in] vka          Allocator for creating kernel objects. If the server is
 *                         configured to support a dynamic number of irqs, this
 *                         resource allocator must remain available until the system
 *                         reaches a steady state.
 * @param[in] cspace       The cspace of the current thread
 * @param[in] priority     The priority of spawned threads.
 * @param[in] simple       A simple interface for creating IRQ caps
 * @param[in] sync_ep      The synchronous endpoint to send IRQ's to
 * @param[in] label        A label to use when sending a synchronous IPC
 * @param[in] nirqs        The maximum number of irqs to support.
 *                         -1 will set up a dynamic system, however, the
 *                         appropriate resource managers must remain valid for
 *                         the life of the server.
 * @param[out] irq_server  An IRQ server structure to initialise.
 * @return                 0 on success
 */
int irq_server_new(vspace_t* vspace, vka_t* vka, seL4_CPtr cspace, seL4_Word priority,
                   simple_t *simple, seL4_CPtr sync_ep, seL4_Word label,
                   int nirqs, irq_server_t* irq_server);

/**
 * Enable an IRQ and register a callback function
 * @param[in] irq_server   The IRQ server which shall be responsible for the IRQ.
 * @param[in] irq          The IRQ number to register for
 * @param[in] cb           A callback function to call when the requested IRQ arrives.
 * @param[in] token        Client data which should be passed to the registered call
 *                         back function.
 * @return                 On success, returns a handle to the irq data. Otherwise,
 *                         returns NULL
 */
struct irq_data* irq_server_register_irq(irq_server_t irq_server, irq_t irq,
                                         irq_handler_fn cb, void* token);

/**
 * Redirects control to the IRQ subsystem to process an arriving IRQ.
 * The server will read the appropriate message registers to retrieve the
 * information that it needs.
 * @param[in] irq_server   The IRQ server which is responsible for the received IRQ.
 */
void irq_server_handle_irq_ipc(irq_server_t irq_server);

/**
 * Waits on the IRQ delivery endpoint for the next IRQ.
 * If an IPC is received, but the label does not match that which was assigned
 * to the IRQ server, the message info and badge are returned to the caller,
 * much like seL4_Wait(...). If the label does match,
 * irq_server_handle_irq_ipc(...) will be called before returning.
 * @param[in]  irq_server  A handle to the IRQ server
 * @param[out] badge_ret   If badge_ret is not NULL, the received badge will be
 *                         written to badge_ret.
 * @return                 The seL4_MessageInfo_t structure as provided by the
 *                         seL4 kernel in response to a seL4_Wait call. The
 *                         caller may check that the label matches that which
 *                         was registered for the IRQ server in order to
 *                         determine if the received event was destined for the
 *                         IRQ server or if the thread was activated by some
 *                         other IPC event.
 */
seL4_MessageInfo_t irq_server_wait_for_irq(irq_server_t irq_server, seL4_Word* badge_ret);

