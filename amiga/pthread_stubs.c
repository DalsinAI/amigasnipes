/*
 * What libgcc's unwinder asks of POSIX threads (it registers the frame
 * tables of a C++ program under a lock), for a program that runs in one
 * thread: Snipes' C++ all runs in its own process, and acsdl12's mixer
 * process only calls amiga/acsdl_sound.cpp's tone callback, which takes no
 * lock. As amigadaggorath's, cut to what is linked.
 * The AmiCradle Team. Under the same terms as Snipes.
 */
int pthread_mutex_lock(void *m) { (void)m; return 0; }
int pthread_mutex_unlock(void *m) { (void)m; return 0; }
int pthread_cancel(unsigned long t) { (void)t; return 0; }
