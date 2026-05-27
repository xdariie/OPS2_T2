L8: Mages' Dispatch (The Telepathic Switchboard)
Archibald and Eleonora’s "Mages’ Chess" was a massive success, but it caused a huge spike in magical network traffic. To manage the chaos, the Grand Council has tasked you with building a UDP Dispatch Server.

However, unlike the binary datagrams used in chess, junior mages are notoriously bad at structuring data. They send their spell requests as raw, human-readable text strings. Your server must parse these telepathic strings, organize them by urgency, and process them safely without crashing the weave (or your program).

Usage
The program takes one argument, which is the UDP port on which the server listens:

Bash
./sop-dispatch <port>
Stages
Stage 1: The String Weaver (6 pts.)
The server waits for UDP datagrams on the given port. The messages are raw ASCII strings (up to 512 characters). Your primary task is to parse these strings. You must use string tokenization functions (strtok or strtok_r).

Accept messages in the following exact format:
SUBMIT <Priority> <MageName> : <Spell1>-<X1>,<Y1> ; <Spell2>-<X2>,<Y2>

<Priority> is an integer from 1 to 5 (1 being highest urgency).

<MageName> is a single word (max 16 characters).

The payload after the colon : contains 1 or more spells separated by semicolons ;.

Each spell consists of a name, a hyphen -, and X,Y coordinates separated by a comma ,.

Example Datagram:

Plaintext
SUBMIT 2 Archibald : Fireball-10,20 ; Divination-5,5
After receiving a datagram, parse it and print the breakdown to standard output:

Plaintext
[Received] Priority 2 from Archibald.
  -> Spell: Fireball at [10, 20]
  -> Spell: Divination at [5, 5]
If the string is malformed, missing parts, or uses an unknown command (e.g., a typo like SBMIT), print [Error] Malformed telepathy from an apprentice and continue server operation. The program ends after receiving and handling 5 valid messages.

Stage 2: The Thread-Safe Priority Queue (7 pts.)
Notice: Active waiting (busywaiting) is forbidden. A Thread Pool is also forbidden.

Instead of processing requests immediately, you must design and implement your own Thread-Safe Priority Queue (e.g., a Linked List sorted by priority 1-5).

For every valid SUBMIT datagram received, the main server loop must spawn a new, detached thread.

This thread is responsible for parsing the string. (Hint: Standard strtok maintains internal state and is not thread-safe! Using it across multiple threads will corrupt your strings. Find the thread-safe alternative).

The thread allocates a new node, inserts it into your custom Priority Queue in the correct sorted position, and prints:

Plaintext
[Queued] <MageName>'s request safely stored.
The thread then terminates.

Synchronization: Multiple messenger threads will be attempting to parse strings and insert nodes into this queue at the exact same time. Use a mutex to protect your custom data structure so the pointers do not get corrupted.

Stage 3: The Archmage Consumer (6 pts.)
Add a single "Archmage" thread that starts when the server boots. This thread acts as the consumer for your Priority Queue.

The Archmage thread continuously checks the queue.

If the queue is empty, the Archmage must sleep using a condition variable (do not use sleep() or spin-locks).

When a detached thread adds a new request to the queue, it signals the condition variable to wake the Archmage.

The Archmage pops the highest-priority request (the one closest to Priority 1) from the front of the queue.

The Archmage "casts" the spells by waiting 200 ms per spell in the request, then prints:

Plaintext
[Processed] Archmage completed request for <MageName>.
Stage 4: The Impossible Revocation (5 pts.)
(Warning: This stage requires highly precise thread synchronization).

Apprentices often make mistakes and need to cancel specific spells before the Archmage casts them. Accept a new UDP command format:
REVOKE <MageName> : <SpellName>

When this datagram is received, spawn a detached thread. This thread must lock the priority queue and iterate through it to find the pending request belonging to <MageName>.

If found, it must modify the string/node to remove <SpellName> from their list of spells.

If <SpellName> was the only spell in their request, the thread must safely detach and delete the entire node from the middle of the priority queue, reconnecting the previous and next pointers.

It then prints:

Plaintext
[Revoked] <SpellName> cancelled for <MageName>.
If the Archmage has already processed the request, print:

Plaintext
[Too Late] The spell was already cast.
The Trap: You must ensure that removing a node from the middle of the queue does not deadlock the Archmage thread (which is trying to pop from the front) or crash another detached thread (which is trying to insert a new request).
