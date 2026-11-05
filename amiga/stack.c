/* Snipes keeps its state in globals and recurses little, but libnix's
 * startup gives a Shell's 4 KB unless asked: this much, as for our other
 * ports. Started from Workbench, what the game writes (why it could not
 * open the screen, say) goes to a window that opens only when something is
 * written.
 * The AmiCradle Team. Under the same terms as Snipes. */
unsigned long __stack = 128 * 1024;
const char *__stdiowin = "CON:0/12/640/200/Snipes/AUTO/CLOSE/WAIT";
