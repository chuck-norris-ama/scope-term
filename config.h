#ifndef CONFIG_H
#define CONFIG_H

#define IRC_SERVER "chat.freenode.net"
/* Set your nickname and uncomment the following line */
// #define IRC_NAME   "YOUR_NICK"

#ifndef IRC_NAME
# error "Set your nick in config.h!"
# define IRC_NAME ""
#endif

#endif
