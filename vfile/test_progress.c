#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

struct progress_bar {
    size_t  total;
    size_t  prev;
    size_t  point;
    int     width;
    int     state;
    int     is_tty;
    size_t  prev_n;
};



static
int progress (void *clientp, size_t dltotal, size_t dlnow,
              size_t ultotal, size_t ulnow)
{
  /* The original progress-bar source code was written for curl by Lars Aas,
     and this new edition inherites some of his concepts. */
    struct progress_bar *bar;
    char   line[256], outline[256], format[40];
    float  frac, percent;
    int    barwidth, n;
    size_t total;
    
    
    bar = (struct progress_bar *)clientp;
    total = dltotal + ultotal;

    bar->point = dlnow + ulnow; /* we've come this far */

    if (total == 0) {
        int prevblock = bar->prev / 1024;
        int thisblock = bar->point / 1024;
        while ( thisblock > prevblock ) {
            printf(".");
            prevblock++;
        }
        
    } else if (bar->state < 2) {
        bar->state = 1;         
        frac = (float) bar->point / (float) total;
        percent = frac * 100.0f;
        barwidth = bar->width - 7;
        n = (int) (((float)barwidth) * frac);

        if (n - (int)bar->prev_n)
            return 0;
        
        //assert(n < (int)sizeof(line) - 1);
            
        if (!bar->is_tty) {
            int k;
            
            k = n - bar->prev_n;
            memset(line, '.', k);
            line[k] = '\0';
            printf("%s", line);
            
        } else {
            memset(line, '.', n);
            line[n] = '\0';
            
            snprintf(format, sizeof(format), "%%-%ds %%5.1f%%%%", barwidth );
            snprintf(outline, sizeof(outline), format, line, percent );
            
            printf("\r%s", outline);
            
        }
        bar->prev_n = n;
        if (total == bar->point)
            bar->state = 2;
    }
    
        
    bar->prev = bar->point;
    
    if (total == bar->point && bar->state == 2) {
        if (bar->is_tty)
            printf("\n");
        else
            printf(" Done\n");
        bar->state = 3;
    }

    fflush(stdout);
    //usleep(40);
    return 0;
}

#define MAX 600000
int main(int argc, char *argv[])
{
    
    int i;
    int is_tty = isatty();
    while(1) {
        struct progress_bar bar = {0, 0, 0, 75, 0, is_tty, 0};
        for (i=0; i<= MAX; i++) {
            progress (&bar, MAX, i, 0, 0);
        }
    }
    
    
}
