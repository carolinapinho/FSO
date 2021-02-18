/**
 * @file
 *
 * \brief A hospital pediatric urgency with a Manchester triage system.
 */

#include  <stdio.h>
#include  <stdlib.h>
#include  <string.h>
#include  <libgen.h>
#include  <unistd.h>
#include  <sys/wait.h>
#include  <sys/types.h>
#include  <thread.h>
#include  <math.h>
#include  <stdint.h>
#include  <signal.h>
#include  <utils.h>
#include  "settings.h"
#include  "pfifo.h"

#include "thread.h"
//#include "process.h"

#include <iostream>

#define USAGE "Synopsis: %s [options]\n" \
   "\t----------+-------------------------------------------\n" \
   "\t  Option  |          Description                      \n" \
   "\t----------+-------------------------------------------\n" \
   "\t -p num   | number of patients (dfl: 4)               \n" \
   "\t -n num   | number of nurses (dfl: 1)                 \n" \
   "\t -d num   | number of doctors (dfl: 1)                \n" \
   "\t -h       | this help                                 \n" \
   "\t----------+-------------------------------------------\n"

//variaveis de condição	(1 para cada paciente)
//inicializar em baixo
static pthread_cond_t vcond[MAX_PATIENTS];
static pthread_mutex_t accessCR =  PTHREAD_MUTEX_INITIALIZER;

bool nsai;
bool dsai;
/**
 * \brief Patient data structure
 */
typedef struct
{
   char name[MAX_NAME+1];
   int done; // 0: waiting for consultation; 1: consultation finished
} Patient;

typedef struct
{
    int num_patients;
    Patient all_patients[MAX_PATIENTS];
    PriorityFIFO triage_queue;
    PriorityFIFO doctor_queue;
} HospitalData;
typedef struct
{
	unsigned int id;
	
} ARGV;

HospitalData * hd = NULL;

/**
 *  \brief patient verification test
 */
#define check_valid_patient(id) do { check_valid_id(id); check_valid_name(hd->all_patients[id].name); } while(0)

int random_manchester_triage_priority();
void new_patient(Patient* patient); // initializes a new patient
void random_wait();

/* ************************************************* */

/* changes may be required to this function */
void init_simulation(uint32_t np)
{
   printf("Initializing simulation\n");
   mutex_lock(&accessCR);
   hd = (HospitalData*)mem_alloc(sizeof(HospitalData)); // mem_alloc is a malloc with NULL pointer verification
   memset(hd, 0, sizeof(HospitalData));
   hd->num_patients = np;
   init_pfifo(&hd->triage_queue);
   init_pfifo(&hd->doctor_queue);
   mutex_unlock(&accessCR);
}

/* ************************************************* */


void nurse_iteration(void *argp)
{
	
   printf("\e[34;01mNurse: get next patient\e[0m\n");
   uint32_t patient = retrieve_pfifo(&hd->triage_queue);
   check_valid_patient(patient);
   uint32_t priority;
   printf("\e[34;01mNurse: evaluate patient %u priority\e[0m\n", patient);
   if(patient==MAX_ID){
	   priority = 16;  //prioridade minima atribuida ao dummy
	}
	else{
		priority = random_manchester_triage_priority();
	}
   printf("\e[34;01mNurse: add patient %u with priority %u to doctor queue\e[0m\n", patient, priority);
   insert_pfifo(&hd->doctor_queue, patient, priority);
   if(patient==MAX_ID){
	   nsai=true;
	}

   
}
void *nurse(void *argp)
{
	ARGV* argv = (ARGV*)argp;
	//for (unsigned int i = 0; i < argv->niter; i++)
	//{					// para conseguir compilar
	printf("nurse %u", argv->id);
	while(1){
		nurse_iteration(argv); 
		if(nsai==true){
		 printf("Nurse vai nanar");
			break;
		}
	}
	return NULL;
}
/* ************************************************* */

void doctor_iteration(void *argp)
{
	ARGV* argv = (ARGV*)argp;		//so para imprimir o id dos doutores
   printf("\e[32;01mDoctor: get next patient\e[0m\n");
   uint32_t patient = retrieve_pfifo(&hd->doctor_queue);
   check_valid_patient(patient);
   printf("\e[32;01mDoctor: treat patient %u\e[0m\n", patient);
   random_wait();
   printf("\e[32;01mDoctor: patient %u treated\e[0m\n", patient);
   mutex_lock(&accessCR);
   hd->all_patients[patient].done = 1;
   cond_broadcast(&vcond[patient]);
   mutex_unlock(&accessCR);
   if(patient==MAX_ID){
	   dsai=true;
	}
}
void *doctor(void *argp)
{
	ARGV* argv = (ARGV*)argp;
	//for (unsigned int i = 0; i < argv->niter; i++)
	//{					// para conseguir compilar
	printf("doutor %u", argv->id);
	while(1){
		doctor_iteration(argv); 
		if(dsai==true){
			 printf("Medico vai nanar");
			break;
		}
	}
	return NULL;
}
/* ************************************************* */

void patient_goto_urgency(int id)
{

   new_patient(&hd->all_patients[id]);
   check_valid_name(hd->all_patients[id].name);
   printf("\e[30;01mPatient %s (number %u): get to hospital\e[0m\n", hd->all_patients[id].name, id);
   insert_pfifo(&hd->triage_queue, id, 1); // all elements in triage queue with the same priority!
	//não posso por lock pq o insert tem um wait e pode ficar em deadlock

}

/* changes may be required to this function */
void patient_wait_end_of_consultation(int id)
{
   check_valid_name(hd->all_patients[id].name);
   mutex_lock(&accessCR);
   while(hd->all_patients[id].done == 0){
	
	   cond_wait(&vcond[id],&accessCR);
   }
   hd->all_patients[id].done = 0;
   mutex_unlock(&accessCR);
   printf("\e[30;01mPatient %s (number %u): health problems treated\e[0m\n", hd->all_patients[id].name, id);
}

/* changes are required to this function */
void patient_life(int id)
{
   patient_goto_urgency(id);
   //nurse_iteration();  // to be deleted in concurrent version
   //doctor_iteration(); // to be deleted in concurrent version
   patient_wait_end_of_consultation(id);
   memset(&(hd->all_patients[id]), 0, sizeof(Patient)); // patient finished
}

void *patient(void *argp)
{
	ARGV* argv = (ARGV*)argp;	//ler argumentos
								//experimentar com argp
	printf("paciente %u", argv->id);
	patient_life(argv->id); 
	
	return NULL;
}
/* ************************************************* */

int main(int argc, char *argv[])
{
   uint32_t npatients = 4;  ///< number of patients
   uint32_t nnurses = 1;    ///< number of triage nurses
   uint32_t ndoctors = 1;   ///< number of doctors
	
   /* command line processing */
   int option;
   while ((option = getopt(argc, argv, "p:n:d:h")) != -1)
   {
      switch (option)
      {
         case 'p':
            npatients = atoi(optarg);
            if (npatients < 1 || npatients > MAX_PATIENTS)
            {
               fprintf(stderr, "Invalid number of patients!\n");
               return EXIT_FAILURE;
            }
            break;
         case 'n':
            nnurses = atoi(optarg);
            if (nnurses < 1)
            {
               fprintf(stderr, "Invalid number of nurses!\n");
               return EXIT_FAILURE;
            }
            break;
         case 'd':
            ndoctors = atoi(optarg);
            if (ndoctors < 1)
            {
               fprintf(stderr, "Invalid number of doctors!\n");
               return EXIT_FAILURE;
            }
            break;
         case 'h':
            printf(USAGE, basename(argv[0]));
            return EXIT_SUCCESS;
         default:
            fprintf(stderr, "Non valid option!\n");
            fprintf(stderr, USAGE, basename(argv[0]));
            return EXIT_FAILURE;
      }
   }

   /* start random generator */
   srand(getpid());

   /* init simulation */
   init_simulation(npatients);

	//inicializar as variaveis de condição
	for (unsigned int i = 0; i < npatients; i++) {
		vcond[i] = PTHREAD_COND_INITIALIZER;
	}
	nsai=false;
	dsai=false;
   /* dummy code to show a very simple sequential behavior 
   for(uint32_t i = 0; i < npatients; i++)
   {
      printf("\n");
      random_wait(); // random wait for patience creation
      patient_life(i);
   }
   */
   pthread_t pthr[npatients+1];		//+dummy
   ARGV  parg[npatients+1];			//+dummy
   
	

   for(unsigned int i=0; i<npatients;i++){
	   parg[i].id = i;
   //nome da thread, Null, como eu a vou invocar,argumentos(ARGV)
   printf(">> Patient thread %d has started <<\n", parg[i].id);
	   thread_create(&pthr[i],NULL, patient,&parg[i]);
	   
   }
   
   pthread_t nthr[nnurses];
   ARGV  narg[nnurses];
   for(unsigned int i=0; i<nnurses;i++){
	   narg[i].id = i;
   //nome da thread, Null, como eu a vou invocar,argumentos('ARGV' void*)
	   printf(">> nurse thread %d has started <<\n", narg[i].id);
	   thread_create(&nthr[i],NULL, nurse,&narg[i]);
	   
	   
   }
   
   pthread_t dthr[ndoctors];
   ARGV  darg[ndoctors];
   for(unsigned int i=0; i<ndoctors;i++){
	   darg[i].id = i;
   //nome da thread, Null, como eu a vou invocar,argumentos(ARGV)
		printf(">> doctor thread %d has started <<\n", darg[i].id);
	   thread_create(&dthr[i],NULL, doctor,&darg[i]);
	   
   }
   parg[npatients].id =MAX_ID;		//npatients +1-1 é o indice do dummy
   thread_create(&pthr[npatients],NULL, patient,&parg[npatients]);
   printf(">> dummy Patient thread %d has started <<\n", parg[npatients].id);
  //-----------------------------------------------------------
  //terminar threads
	//terminar dummy
	
    //resto das threads
	  for (unsigned int i = 0; i < npatients; i++)
    {
        thread_join(pthr[i], NULL);
        printf("Patients thread %d has terminated\n", i);
    }
	 for (unsigned int i = 0; i < nnurses; i++)
    {
        thread_join(nthr[i], NULL);
        printf("Nurse thread %d has terminated\n", i);
    }
    for (unsigned int i = 0; i < ndoctors; i++)
    {
        thread_join(dthr[i], NULL);
        printf("Doctor thread %d has terminated\n", i);
    }
   thread_join(pthr[npatients], NULL);
    printf("Dummy Patients thread %d has terminated\n", npatients);
   
   return EXIT_SUCCESS;
}


/* YOU MAY IGNORE THE FOLLOWING CODE */

int random_manchester_triage_priority()
{
   int result;
   int perc = (int)(100*(double)rand()/((double)RAND_MAX)); // in [0;100]
   if (perc < 10)
      result = RED;
   else if (perc < 30)
      result = ORANGE;
   else if (perc < 50)
      result = YELLOW;
   else if (perc < 75)
      result = GREEN;
   else
      result = BLUE;
   return result;
}

static char **names = (char *[]) {"Ana", "Miguel", "Luis", "Joao", "Artur", "Maria", "Luisa", "Mario", "Augusto", "Antonio", "Jose", "Alice", "Almerindo", "Gustavo", "Paulo", "Paula", NULL};

char* random_name()
{
   static int names_len = 0;
   if (names_len == 0)
   {
      for(names_len = 0; names[names_len] != NULL; names_len++)
         ;
   }

   return names[(int)(names_len*(double)rand()/((double)RAND_MAX+1))];
}

void new_patient(Patient* patient)
{
   strcpy(patient->name, random_name());
   patient->done = 0;
}

void random_wait()
{
   usleep((useconds_t)(MAX_WAIT*(double)rand()/(double)RAND_MAX));
}

