#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <ctype.h>
#include <fcntl.h>
#include <sys/file.h>
#include "./struct/customer.h"
#include "./struct/loan.h"
#include "./struct/transaction.h"
#include "./struct/feedback.h"
#include "./struct/employee.h"
#include "./struct/manager.h"
#include "./struct/admin.h"

#define PORT 8080
#define BUFFER_SIZE 1024

/////////////////////////Transaction////////////////////////


void log_transaction(int customerID, const char *type, float amount) 
{
    struct transaction txn;
    static int transaction_counter = 0;

    txn.transactionID = ++transaction_counter;
    txn.customerID = customerID;
    strncpy(txn.type, type, sizeof(txn.type) - 1);
    txn.type[sizeof(txn.type) - 1] = '\0'; 
    txn.amount = amount;

    time_t now = time(NULL);
    strftime(txn.timestamp, sizeof(txn.timestamp), "%Y-%m-%d %H:%M:%S", localtime(&now));

    int fd = open("./data/transaction.data", O_WRONLY | O_APPEND | O_CREAT, 0644);
    if (fd < 0) 
    {
        perror("Failed to open transaction log");
        return;
    }

    if (flock(fd, LOCK_EX) < 0) {
        perror("Failed to lock the transaction log");
        close(fd);
        return;
    }

    if (write(fd, &txn, sizeof(struct transaction)) != sizeof(struct transaction)) 
    {
        perror("Failed to write transaction to log");
    }

    flock(fd, LOCK_UN);
    close(fd);
}

/////////////////////////Transaction////////////////////////

/////////////////////////Customer////////////////////////

float get_balance(int uid)
{
    int fd = open("./data/customer.data", O_RDONLY);
    if (fd < 0)
    {
        perror("Failed to open the file");
        return -1;
    }

    struct customer c;  
    int found = 0;      
    float balance = -1; 

    while (read(fd, &c, sizeof(struct customer)) == sizeof(struct customer))
    {
        printf("UserID : %d, Name: %s %s, Password: %s, Balance: %.2f, Loan: %.2f, Status: %s\n", c.userID, c.firstName, c.lastName, c.password, c.balance, c.loan, c.status);
        if (c.userID == uid)
        {
            printf("User ID %d found. Current balance: %.2f\n", c.userID, c.balance);
            balance = c.balance;
            found = 1;
            break; 
        }
    }

    close(fd); 

    if (!found)
    {
        printf("User ID %d not found.\n", uid);
        return -1; 
    }

    return balance;
}

int deposit(int uid, float amount)
{
    int fd = open("./data/customer.data", O_RDWR); 
    if (fd < 0)
    {
        perror("Failed to open the file");
        return -1;
    }

    if (flock(fd, LOCK_EX) < 0)
    { 
        perror("Failed to acquire lock");
        close(fd);
        return -1;
    }

    struct customer c;
    int found = 0;
    off_t position; 
    while (read(fd, &c, sizeof(struct customer)) == sizeof(struct customer))
    {
        if (c.userID == uid)
        {
            found = 1;
            c.balance += amount;                                    
            position = lseek(fd, -sizeof(struct customer), SEEK_CUR); 
            if (position == -1)
            {
                perror("lseek failed");
                close(fd);
                return -1;
            }
            if (write(fd, &c, sizeof(struct customer)) != sizeof(struct customer))
            {
                perror("Failed to write updated record");
                close(fd);
                return -1;
            } 
            log_transaction(uid, "Deposit", amount);
            break;
        }
    }

    flock(fd, LOCK_UN);
    close(fd);

    if (!found)
    {
        return -1;
    }

    return 0;
}

int withdraw(int uid, float amount)
{

    int fd = open("./data/customer.data", O_RDWR);
    if (fd < 0)
    {
        perror("Failed to open the file");
        return -1;
    }

    if (flock(fd, LOCK_EX) < 0)
    {
        perror("Failed to lock the file");
        close(fd);
        return -1;
    }

    struct customer c; 
    int found = 0;  
    off_t position;
    while (read(fd, &c, sizeof(struct customer)) == sizeof(struct customer))
    {
        if (c.userID == uid)
        {
            if (c.balance < amount)
            {
                flock(fd, LOCK_UN);
                close(fd);
                return -1;
            }

            c.balance -= amount;

            position = lseek(fd, -sizeof(struct customer), SEEK_CUR); 
            if (position == -1)
            {
                perror("lseek failed");
                close(fd);
                return -1;
            }

            if (write(fd, &c, sizeof(struct customer)) != sizeof(struct customer))
            {
                perror("Failed to write updated record");
                flock(fd, LOCK_UN);
                close(fd);
                return -1;
            }
            found = 1;
            log_transaction(uid, "Withdraw", amount);
            break; 
        }
    }

    flock(fd, LOCK_UN);

    close(fd); 

    if (!found)
    {
        printf("User ID %d not found.\n", uid);
        return -1; 
    }

    return 0; 
}

int transfer(int from_uid, int to_uid, float amount)
{
    int fd = open("./data/customer.data", O_RDWR);
    if (fd < 0)
    {
        perror("Failed to open the file");
        return -1;
    }
    printf("\nFrom user: %d to user: %d", from_uid, to_uid);
    if (flock(fd, LOCK_EX) < 0)
    {
        perror("Failed to lock the file");
        close(fd);
        return -1;
    }

    struct customer c, from_customer, to_customer;
    int found_from = 0, found_to = 0;
    off_t form_position, to_position;

    while (read(fd, &c, sizeof(struct customer)) == sizeof(struct customer))
    {
        printf("From UserID : %d, amount : %.2f\n", c.userID, c.balance);
        if (c.userID == from_uid)
        {
            if (c.balance < amount)
            {
                printf("Insufficient funds. Transfer aborted.\n");
                flock(fd, LOCK_UN); 
                close(fd);
                return -1;
            }
            found_from = 1;
            break;
        }
    }

    if (lseek(fd, 0, SEEK_SET) == -1)
    {
        perror("lseek failed");
        close(fd);
        return -1;
    }

    while (read(fd, &to_customer, sizeof(struct customer)) == sizeof(struct customer))
    {
        if (to_customer.userID == to_uid)
        {
            printf("\nTo User ID %d found. Current balance: %.2f\n", to_customer.userID, to_customer.balance);
            found_to = 1;
            to_customer.balance += amount; 
            printf("\nUpdated balance: %.2f\n", to_customer.balance);
            to_position = lseek(fd, -sizeof(struct customer), SEEK_CUR); 
            if (to_position == -1)
            {
                perror("lseek failed");
                close(fd);
                return -1;
            }
            if (write(fd, &to_customer, sizeof(struct customer)) != sizeof(struct customer))
            {
                perror("Failed to write updated record");
                close(fd);
                return -1;
            }
            printf("To transfer successful. New balance: %.2f\n", to_customer.balance);
            log_transaction(to_uid, "Recieved Transfer", amount);
            break;
        }
    }

    if (lseek(fd, 0, SEEK_SET) == -1)
    {
        perror("lseek failed");
        close(fd);
        return -1;
    }

    if (found_to)
    {
        while (read(fd, &from_customer, sizeof(struct customer)) == sizeof(struct customer))
        {
            printf("Checking at From User ID %d found. Current balance: %.2f\n", from_customer.userID, from_customer.balance);
            if (from_customer.userID == from_uid)
            { 
                printf("From User ID %d found. Current balance: %.2f\n", from_customer.userID, from_customer.balance);
                from_customer.balance -= amount;
                printf("New balance after withdrawal: %.2f\n", from_customer.balance);

                form_position = lseek(fd, -sizeof(struct customer), SEEK_CUR); 
                if (form_position == -1)
                {
                    perror("lseek failed");
                    close(fd);
                    return -1;
                }

                if (write(fd, &from_customer, sizeof(struct customer)) != sizeof(struct customer))
                {
                    perror("Failed to write updated record");
                    flock(fd, LOCK_UN);
                    close(fd);
                    return -1;
                }
                printf("From transfer successful. New balance: %.2f\n", from_customer.balance);
                found_from = 1;
                log_transaction(from_uid, "Transfer", amount);
                break;
            }
        }
    }

    flock(fd, LOCK_UN);
    close(fd);

    printf("Transfer successful.\n");
    return 0;
}

int apply_loan(int uid, float amount)
{
    int fd = open("./data/loan.data", O_WRONLY | O_APPEND | O_CREAT, 0644);

    if (fd < 0)
    {
        perror("Failed to open loan data file");
        return -1;
    }

    if (flock(fd, LOCK_EX) < 0)
    {
        perror("Failed to acquire lock");
        close(fd);
        return -1;
    }

    struct loan new_loan;
    struct customer c;
    c.loan = amount;
    new_loan.userID = uid;
    new_loan.amount = amount;
    strcpy(new_loan.status, "Pending");

    if (write(fd, &new_loan, sizeof(struct loan)) != sizeof(struct loan))
    {
        perror("Failed to write loan data");
        flock(fd, LOCK_UN);
        close(fd);
        return -1;
    }

    printf("Loan application submitted.\n");
    flock(fd, LOCK_UN);
    close(fd);

    return 0;
}

int change_cust_password(int uid, const char *new_password)
{
    int fd = open("./data/customer.data", O_RDWR);
    if (fd == -1)
    {
        perror("Error opening customer file");
        return -1;
    }

    struct flock lock;
    lock.l_type = F_WRLCK;
    lock.l_whence = SEEK_SET;
    lock.l_start = 0;
    lock.l_len = 0;

    if (fcntl(fd, F_SETLK, &lock) == -1)
    {
        perror("Error locking file");
        close(fd);
        return -1;
    }

    struct customer c;
    int found = 0;
    off_t position;

    while (read(fd, &c, sizeof(struct customer)) == sizeof(struct customer))
    {
        if (c.userID == uid)
        {
            position = lseek(fd, -sizeof(struct customer), SEEK_CUR);
            if (position == -1)
            {
                perror("lseek failed");
                close(fd);
                return -1;
            }
            strncpy(c.password, new_password, sizeof(c.password) - 1);
            c.password[sizeof(c.password) - 1] = '\0';
            if (write(fd, &c, sizeof(struct customer)) != sizeof(struct customer))
            {
                perror("Failed to write updated record");
                flock(fd, LOCK_UN);
                close(fd);
                return -1;
            }
            found = 1;
            break;
        }
    }

    lock.l_type = F_UNLCK; 
    fcntl(fd, F_SETLK, &lock);
    close(fd);
    if (!found)
    {
        printf("User ID %d not found.\n", uid);
        return -1;
    }

    printf("Password changed successfully.\n");
    return 0;
}

int add_feedback(int customerID, const char *message) 
{
    int fd = open("./data/feedback.data", O_WRONLY | O_APPEND | O_CREAT, 0644);
    if (fd == -1) 
    {
        perror("Error opening feedback file");
        return -1;
    }

    if (flock(fd, LOCK_EX) < 0) 
    {
        perror("Error locking feedback file");
        close(fd);
        return -1;
    }

    static int feedback_counter = 0; 
    struct feedback new_feedback;
    new_feedback.feedbackID = ++feedback_counter; 
    new_feedback.customerID = customerID;
    strncpy(new_feedback.message, message, sizeof(new_feedback.message) - 1);
    new_feedback.message[sizeof(new_feedback.message) - 1] = '\0'; 
    strncpy(new_feedback.status, "Pending", sizeof(new_feedback.status) - 1);
    new_feedback.status[sizeof(new_feedback.status) - 1] = '\0'; 

    if (write(fd, &new_feedback, sizeof(struct feedback)) != sizeof(struct feedback)) 
    {
        perror("Failed to write feedback entry");
        flock(fd, LOCK_UN);
        close(fd);
        return -1;
    }

    flock(fd, LOCK_UN);
    close(fd);

    return 0; 
}

int view_transaction_history(int sock, int userID) 
{
    int fd = open("./data/transaction.data", O_RDONLY); 
    if (fd == -1) 
    {
        perror("Error opening transactions file");
        return -1;
    }

    struct flock lock;
    lock.l_type = F_RDLCK;
    lock.l_whence = SEEK_SET;
    lock.l_start = 0;
    lock.l_len = 0;

    if (fcntl(fd, F_SETLK, &lock) == -1) 
    {
        perror("Error locking file");
        close(fd);
        return -1;
    }

    struct transaction txn;
    int transaction_found = 0;
    char buffer[256]; 

    while (read(fd, &txn, sizeof(struct transaction)) == sizeof(struct transaction)) 
    {
        if (txn.customerID == userID) 
        {
            snprintf(buffer, sizeof(buffer), "Transaction ID: %d, Type: %s, Amount: %.2f, Timestamp: %s\n", txn.transactionID, txn.type, txn.amount, txn.timestamp);

            if (write(sock, buffer, strlen(buffer)) == -1) 
            {
                perror("Failed to send transaction data");
                flock(fd, LOCK_UN); 
                close(fd);
                return -1;
            }
            transaction_found = 1;
        }
    }

    if (!transaction_found) 
    {
        snprintf(buffer, sizeof(buffer), "No transaction history found for UserID: %d\n", userID);
        write(sock, buffer, strlen(buffer));
    }

    flock(fd, LOCK_UN);
    close(fd);

    return 0;  
}

void handle_logout(int client_sock)
{
    send(client_sock, "Logged Out...\n", strlen("Logged Out...\n"), 0);
    printf("User logged out.\n");
}


void handle_customer_requests(int sock, const char *user_id)
{
    int id = atoi(user_id);
    while (1)
    {
        char buffer[BUFFER_SIZE];
        int bytes_read = read(sock, buffer, BUFFER_SIZE);
        buffer[bytes_read] = '\0';

        if (strcmp(buffer, "VIEW_BALANCE") == 0)
        {
            float balance = get_balance(id);
            snprintf(buffer, sizeof(buffer), "%.2f", balance);
            write(sock, buffer, strlen(buffer));
        }
        else if (strcmp(buffer, "DEPOSIT_MONEY") == 0)
        {
            float amount;
            int bytes_read = read(sock, buffer, sizeof(buffer) - 1);
            buffer[bytes_read] = '\0';
            sscanf(buffer, "%f", &amount);

            int status = deposit(id, amount);
            if(status == 0)
                snprintf(buffer, sizeof(buffer), "Money deposited successfuly");
            else
                snprintf(buffer, sizeof(buffer), "Failed to deposit money");

            write(sock, buffer, strlen(buffer));
        }
        else if (strcmp(buffer, "WITHDRAW_MONEY") == 0)
        {
            float amount;
            int bytes_read = read(sock, buffer, sizeof(buffer) - 1);
            buffer[bytes_read] = '\0';
            sscanf(buffer, "%f", &amount);
            int status = withdraw(id, amount);
            if(status == 0)
                snprintf(buffer, sizeof(buffer), "Money withdrawn successfuly");
            else
                snprintf(buffer, sizeof(buffer), "Failed to withdraw money");

            write(sock, buffer, strlen(buffer));
        }
        else if (strcmp(buffer, "TRANSFER_FUNDS") == 0)
        {
            int to_user;
            float amount;
            int bytes_read = read(sock, buffer, sizeof(buffer) - 1);
            buffer[bytes_read] = '\0';
            sscanf(buffer, "%d", &to_user);
            bytes_read = read(sock, buffer, sizeof(buffer) - 1);
            buffer[bytes_read] = '\0';
            sscanf(buffer, "%f", &amount);
            int status = transfer(id, to_user, amount);
            if (status == -1)
                snprintf(buffer, sizeof(buffer), "Failed to transfer funds\n");
            else
                snprintf(buffer, sizeof(buffer), "Funds Transfer Successful\n");
            write(sock, buffer, strlen(buffer));
        }
        else if (strcmp(buffer, "APPLY_LOAN") == 0)
        {
            float amount;
            int bytes_read = read(sock, buffer, sizeof(buffer) - 1);
            buffer[bytes_read] = '\0'; 
            sscanf(buffer, "%f", &amount);
            int status = apply_loan(id, amount); 
            if(status == 0)
                snprintf(buffer, sizeof(buffer), "Applied for loan successfuly");
            else
                snprintf(buffer, sizeof(buffer), "Failed to apply for loan");

            write(sock, buffer, strlen(buffer));
        }
        else if (strcmp(buffer, "CHANGE_PWD") == 0) 
        {
            char new_pwd[50];
            read(sock, new_pwd, sizeof(new_pwd));
            new_pwd[sizeof(new_pwd) - 1] = '\0';
            size_t len = strlen(new_pwd);
            if (len > 0 && new_pwd[len - 1] == '\n') {
                new_pwd[len - 1] = '\0';
            }
            int status = change_cust_password(id, new_pwd);
            if (status == 0)
                snprintf(buffer, sizeof(buffer), "Password changed\n");
            else
                snprintf(buffer, sizeof(buffer), "Failed to change password try again...\n");
            write(sock, buffer, strlen(buffer));
        }
        else if (strcmp(buffer, "FEEDBACK") == 0) 
        {
            char feedback[500];
            read(sock, feedback, sizeof(feedback));
            feedback[sizeof(feedback) - 1] = '\0';
            size_t len = strlen(feedback);
            if (len > 0 && feedback[len - 1] == '\n') 
            {
                feedback[len - 1] = '\0';
            }
            int status = add_feedback(id, feedback);
            if(status == 0)
                snprintf(buffer, sizeof(buffer), "Successfully Submitted");
            else
                snprintf(buffer, sizeof(buffer), "Failed to submit feedback");
            write(sock, buffer, strlen(buffer));
        }
        else if (strcmp(buffer, "VIEW_TRANSACTION") == 0) 
        {
            int status = view_transaction_history(sock, id);
            if (status == -1)
            {   
                snprintf(buffer, sizeof(buffer), "Unable to show transaction history due to unknown error");
                write(sock, buffer, strlen(buffer));
            }
            write(sock, "END", strlen("END"));
        }
        else if (strcmp(buffer, "LOGOUT") == 0)
        {
            handle_logout(sock);
            break;
        }
    }
}

/////////////////////////Customer/////////////////////////

/////////////////////////Employee/////////////////////////
int add_customer(int uid, const char *fname, const char *lname, const char *pwd, float bal) 
{
    int fd = open("./data/customer.data", O_WRONLY | O_APPEND | O_CREAT, 0644);
    if (fd < 0) 
    {
        perror("Failed to open the customer file");
        return -1;
    }

    struct flock lock;
    lock.l_type = F_WRLCK;
    lock.l_whence = SEEK_END;
    lock.l_start = 0;
    lock.l_len = 0; 

    if (fcntl(fd, F_SETLKW, &lock) == -1) 
    {
        perror("Failed to lock the file");
        close(fd);
        return -1;
    }

    struct customer new_customer;
    new_customer.userID = uid;
    strncpy(new_customer.firstName, fname, sizeof(new_customer.firstName) - 1);
    new_customer.firstName[sizeof(new_customer.firstName) - 1] = '\0';
    strncpy(new_customer.lastName, lname, sizeof(new_customer.lastName) - 1);
    new_customer.lastName[sizeof(new_customer.lastName) - 1] = '\0';
    strncpy(new_customer.password, pwd, sizeof(new_customer.password) - 1);
    new_customer.password[sizeof(new_customer.password) - 1] = '\0';
    new_customer.balance = bal;
    new_customer.loan = 0;
    strncpy(new_customer.status, "Active", sizeof(new_customer.status) - 1);
    new_customer.status[sizeof(new_customer.status) - 1] = '\0';

    if (write(fd, &new_customer, sizeof(struct customer)) != sizeof(struct customer)) 
    {
        perror("Failed to write new customer to the file");
        fcntl(fd, F_UNLCK, &lock); 
        close(fd);
        return -1;
    }

    lock.l_type = F_UNLCK;
    fcntl(fd, F_SETLK, &lock);
    close(fd);

    printf("New customer (ID: %d) added successfully.\n", uid);
    return 0; 
}

int fetch_assigned_loan_details(int *loan_ids, int loan_count, char *details) 
{
    struct loan ln;
    FILE *loan_file = fopen("./data/loan.data", "rb");
    if (!loan_file) {
        perror("Error opening loan data file");
        return -1;
    }

    char temp[100];
    strcpy(details, "Assigned Loan Details:\n");

    for (int i = 0; i < loan_count; i++) 
    {
        rewind(loan_file);

        while (fread(&ln, sizeof(struct loan), 1, loan_file)) 
        {
            if (ln.userID == loan_ids[i]) 
            {
                sprintf(temp, "Loan ID: %d, Amount: %.2f, Status: %s\n",
                        ln.userID, ln.amount, ln.status);
                strcat(details, temp);  
                break;
            }
        }
    }

    fclose(loan_file);
    return 0;
}

int view_employee_loans(int employee_id, char *details) 
{
    struct employee emp;
    FILE *emp_file = fopen("./data/employee.data", "rb");
    if (!emp_file) {
        perror("Error opening employee data file");
        return -1;
    }

    while (fread(&emp, sizeof(struct employee), 1, emp_file)) 
    {
        if (emp.employeeID == employee_id) 
        {
            fclose(emp_file);
            return fetch_assigned_loan_details(emp.assigned_loans, emp.loan_count, details);
        }
    }

    fclose(emp_file);
    return -1; 
}


int update_loan_status(int loan_id, const char *new_status) 
{
    struct loan ln;
    int loan_found = 0;

    FILE *loan_file = fopen("./data/loan.data", "rb+");
    if (!loan_file) {
        perror("Error opening loan data file");
        return -1;
    }

    while (fread(&ln, sizeof(struct loan), 1, loan_file)) 
    {
        if (ln.userID == loan_id) 
        {
            loan_found = 1;
            strcpy(ln.status, new_status); 
            fseek(loan_file, -sizeof(struct loan), SEEK_CUR);
            fwrite(&ln, sizeof(struct loan), 1, loan_file);
            break;
        }
    }

    fclose(loan_file);
    return loan_found ? ln.amount : -1; 
}

int credit_to_customer(int cust_id, float amount) 
{
    struct customer cust;
    int customer_found = 0;

    FILE *customer_file = fopen("./data/customer.data", "rb+");
    if (!customer_file) 
    {
        perror("Error opening customer data file");
        return -1;
    }

    while (fread(&cust, sizeof(struct customer), 1, customer_file)) 
    {
        if (cust.userID == cust_id) 
        {
            customer_found = 1;
            cust.balance += amount;
            cust.loan += amount;
            fseek(customer_file, -sizeof(struct customer), SEEK_CUR);
            fwrite(&cust, sizeof(struct customer), 1, customer_file);
            break;
        }
    }

    fclose(customer_file);
    return customer_found ? 0 : -1;
}

void view_customer_transactions(int sock) 
{
    char buffer[BUFFER_SIZE];
    int cust_id;

    if (read(sock, buffer, sizeof(buffer) - 1) <= 0) 
    {
        perror("Error reading User ID from client");
        close(sock);
        return;
    }
    sscanf(buffer, "%d", &cust_id);

    FILE *file = fopen("./data/transaction.data", "rb");
    if (!file) 
    {
        perror("Error opening transactions file");
        strcpy(buffer, "Error opening transactions file.\n");
        write(sock, buffer, strlen(buffer));
        return;
    }

    struct transaction txn;
    int found = 0;
    char response[BUFFER_SIZE] = "";

    while (fread(&txn, sizeof(struct transaction), 1, file)) 
    {
        if (txn.customerID == cust_id) 
        {
            found = 1;
            char temp[200];
            snprintf(temp, sizeof(temp), "ID: %d | Type: %s | Amount: %.2f | Date: %s\n", txn.transactionID, txn.type, txn.amount, txn.timestamp);
            strcat(response, temp);
        }
    }
    fclose(file);

    if (!found) 
    {
        strcpy(response, "No transactions found for this customer.\n");
    }
    printf("%s",response);
    write(sock, response, strlen(response));
}


int change_emp_password(int uid, const char *new_password)
{
    int fd = open("./data/employee.data", O_RDWR);
    if (fd == -1)
    {
        perror("Error opening customer file");
        return -1;
    }

    struct flock lock;
    lock.l_type = F_WRLCK;
    lock.l_whence = SEEK_SET;
    lock.l_start = 0;
    lock.l_len = 0;

    if (fcntl(fd, F_SETLK, &lock) == -1)
    {
        perror("Error locking file");
        close(fd);
        return -1;
    }

    struct employee c;
    int found = 0;
    off_t position;

    while (read(fd, &c, sizeof(struct employee)) == sizeof(struct employee))
    {
        if (c.employeeID == uid)
        {
            position = lseek(fd, -sizeof(struct employee), SEEK_CUR);
            if (position == -1)
            {
                perror("lseek failed");
                close(fd);
                return -1;
            }
            strncpy(c.password, new_password, sizeof(c.password) - 1);
            c.password[sizeof(c.password) - 1] = '\0';
            if (write(fd, &c, sizeof(struct employee)) != sizeof(struct employee))
            {
                perror("Failed to write updated record");
                flock(fd, LOCK_UN);
                close(fd);
                return -1;
            }
            found = 1;
            break;
        }
    }

    lock.l_type = F_UNLCK;
    fcntl(fd, F_SETLK, &lock);
    close(fd);
    if (!found)
    {
        printf("User ID %d not found.\n", uid);
        return -1;
    }

    printf("Password changed successfully.\n");
    return 0;
}


void delete_customer(int custID, int sock) 
{
    char buffer[BUFFER_SIZE];
    int found = 0;

    FILE *fp = fopen("./data/customer.data", "rb");
    FILE *temp = fopen("./data/temp_customer.data", "wb");

    if (!fp || !temp) {
        perror("File error");
        strcpy(buffer, "Error accessing customer data.\n");
        write(sock, buffer, strlen(buffer));
        close(sock);
        return;
    }

    struct customer c;

    while (fread(&c, sizeof(struct customer), 1, fp)) 
    {
        if (c.userID == custID) 
        {
            found = 1; 
        } 
        else 
        {
            fwrite(&c, sizeof(struct customer), 1, temp);
        }
    }

    fclose(fp);
    fclose(temp);

    if (found) 
    {
        remove("./data/customer.data");
        rename("./data/temp_customer.data", "./data/customer.data");
        strcpy(buffer, "Customer deleted successfully.\n");
    } 
    else 
    {
        remove("./data/temp_customer.data");
        strcpy(buffer, "Customer not found.\n");
    }

    write(sock, buffer, strlen(buffer));
}




void handle_employee_requests(int sock, const char *user_id)
{
    int id = atoi(user_id);
    char buffer[BUFFER_SIZE];
    while (1)
    {
        
        int bytes_read = read(sock, buffer, BUFFER_SIZE);
        if (bytes_read <= 0) {
            perror("Client disconnected or read error");
            close(sock);
            return;
        }
        buffer[bytes_read] = '\0';

        if (strcmp(buffer, "ADD_CUSTOMER") == 0)
        {
            int uid;
            char fname[30], lname[30], pwd[50], status[20];
            float bal, loan;

            if (read(sock, buffer, sizeof(buffer) - 1) <= 0) {
                perror("Error reading User ID from client");
                close(sock);
                return;
            }
            sscanf(buffer, "%d", &uid);

            bytes_read = read(sock, fname, sizeof(fname) - 1);
            fname[bytes_read] = '\0';
            fname[strcspn(fname, "\n")] = '\0';


            bytes_read = read(sock, lname, sizeof(lname) - 1);
            lname[bytes_read] = '\0';
            lname[strcspn(lname, "\n")] = '\0';

            bytes_read = read(sock, pwd, sizeof(pwd) - 1);
            pwd[bytes_read] = '\0';
            pwd[strcspn(pwd, "\n")] = '\0';

            bytes_read = read(sock, buffer, sizeof(buffer) - 1);
            buffer[bytes_read] = '\0';
            sscanf(buffer, "%f", &bal);

            if (add_customer(uid, fname, lname, pwd, bal) == 0) {
                strcpy(buffer, "Customer added successfully.\n");
            } else {
                strcpy(buffer, "Failed to add customer.\n");
            }
            write(sock, buffer, strlen(buffer));
        }
        else if (strcmp(buffer, "MANAGE_CUSTOMER") == 0) 
        {
            int uid;
            char new_fname[50], new_lname[50], new_status[20], new_password[50], buffer[BUFFER_SIZE];
            float new_balance, new_loan;
            int option, bytes_read;

            bytes_read = read(sock, buffer, sizeof(buffer) - 1);
            buffer[bytes_read] = '\0';
            sscanf(buffer, "%d", &uid);

            struct customer c;
            int fd = open("./data/customer.data", O_RDWR);
            if (fd == -1) {
                perror("Error opening customer file");
                strcpy(buffer, "Error: Unable to open customer file.\n");
                write(sock, buffer, strlen(buffer));
                return;
            }

            struct flock lock;
            lock.l_type = F_WRLCK;
            lock.l_whence = SEEK_SET;
            lock.l_start = 0;
            lock.l_len = 0;

            if (fcntl(fd, F_SETLK, &lock) == -1) {
                perror("Error locking customer file");
                strcpy(buffer, "Error: Unable to lock the file.\n");
                write(sock, buffer, strlen(buffer));
                close(fd);
                return;
            }

            int found = 0;
            off_t position;

            while (read(fd, &c, sizeof(struct customer)) == sizeof(struct customer)) 
            {
                if (c.userID == uid) 
                {
                    found = 1;
                    position = lseek(fd, -sizeof(struct customer), SEEK_CUR);
                    break;
                }
            }

            if (!found) 
            {
                snprintf(buffer, sizeof(buffer), "Customer with ID %d not found.\n", uid);
                write(sock, buffer, strlen(buffer));
                flock(fd, F_UNLCK);
                close(fd);
                return;
            }

            snprintf(buffer, sizeof(buffer), "ID: %d\nName: %s %s\nBalance: %.2f\nLoan: %.2f\nStatus: %s\n", c.userID, c.firstName, c.lastName, c.balance, c.loan, c.status);
            write(sock, buffer, strlen(buffer));

            bytes_read = read(sock, buffer, sizeof(buffer) - 1);
            buffer[bytes_read] = '\0';
            option = atoi(buffer);

            switch (option) 
            {
                case 1:
                    read(sock, new_fname, sizeof(new_fname));
                    new_fname[sizeof(new_fname) - 1] = '\0';
                    size_t len = strlen(new_fname);
                    if (len > 0 && new_fname[len - 1] == '\n') 
                    {
                        new_fname[len - 1] = '\0';
                    }
                    memset(buffer, 0, sizeof(buffer));

                    read(sock, new_lname, sizeof(new_lname));
                    new_lname[sizeof(new_lname) - 1] = '\0';
                    len = strlen(new_lname);
                    if (len > 0 && new_lname[len - 1] == '\n') 
                    {
                        new_lname[len - 1] = '\0';
                    }
                    memset(buffer, 0, sizeof(buffer));

                    bytes_read = read(sock, buffer, sizeof(buffer) - 1);
                    buffer[bytes_read] = '\0';
                    sscanf(buffer, "%f", &new_balance);
                    memset(buffer, 0, sizeof(buffer));

                    bytes_read = read(sock, buffer, sizeof(buffer) - 1);
                    buffer[bytes_read] = '\0';
                    sscanf(buffer, "%f", &new_loan);
                    memset(buffer, 0, sizeof(buffer));

                    read(sock, new_status, sizeof(new_status));
                    new_status[sizeof(new_status) - 1] = '\0';
                    len = strlen(new_status);
                    if (len > 0 && new_status[len - 1] == '\n') 
                    {
                        new_status[len - 1] = '\0';
                    }

                    strncpy(c.firstName, new_fname, sizeof(c.firstName) - 1);
                    strncpy(c.lastName, new_lname, sizeof(c.lastName) - 1);
                    c.balance = new_balance;
                    c.loan = new_loan;
                    strncpy(c.status, new_status, sizeof(c.status) - 1);

                    lseek(fd, position, SEEK_SET);
                    if (write(fd, &c, sizeof(struct customer)) != sizeof(struct customer)) 
                    {
                        perror("Failed to write updated customer record");
                        strcpy(buffer, "Error: Failed to update customer.\n");
                        write(sock, buffer, strlen(buffer));
                    } 
                    else 
                    {
                        strcpy(buffer, "Customer details updated successfully.\n");
                        write(sock, buffer, strlen(buffer));
                    }
                    break;

                case 2: 
                    delete_customer(uid, sock);
                    break;

                case 3: 
                    bytes_read = read(sock, new_password, sizeof(new_password) - 1);
                    new_password[bytes_read] = '\0';
                    len = strlen(new_password);
                    if (len > 0 && new_password[len - 1] == '\n') 
                    {
                        new_password[len - 1] = '\0';
                    }

                    strncpy(c.password, new_password, sizeof(c.password) - 1);

                    lseek(fd, position, SEEK_SET);
                    if (write(fd, &c, sizeof(struct customer)) != sizeof(struct customer)) 
                    {
                        perror("Failed to reset password");
                        strcpy(buffer, "Error: Failed to reset password.\n");
                        write(sock, buffer, strlen(buffer));
                    } 
                    else 
                    {
                        strcpy(buffer, "Password reset successfully.\n");
                        write(sock, buffer, strlen(buffer));
                    }
                    break;

                case 4:
                    strcpy(buffer, "Exiting customer management.\n");
                    write(sock, buffer, strlen(buffer));
                    break;

                default:
                    strcpy(buffer, "Invalid option.\n");
                    write(sock, buffer, strlen(buffer));
                    break;
            }
            flock(fd, F_UNLCK);
            close(fd);
        }
        else if (strcmp(buffer, "VIEW_LOAN_APPL") == 0)
        {
            printf("Processing VIEW_LOAN_APPL request\n");
            char loan_details[BUFFER_SIZE];
            int result = view_employee_loans(id, loan_details);

            if (result == 0) 
            {
                write(sock, loan_details, strlen(loan_details));
            } 
            else 
            {
                strcpy(buffer, "Failed to retrieve loan applications.\n");
                write(sock, buffer, strlen(buffer));
            }
        }
        else if (strcmp(buffer, "APP/REJ_LOANS") == 0)
        {
            printf("Processing APP/REJ_LOANS request\n");
            bytes_read = read(sock, buffer, BUFFER_SIZE);
            buffer[bytes_read] = '\0';
            int loan_id = atoi(buffer);

            bytes_read = read(sock, buffer, BUFFER_SIZE);
            buffer[bytes_read] = '\0';
            char new_status[20];
            strcpy(new_status, buffer);

            float loan_amount = update_loan_status(loan_id, new_status);
            if (loan_amount == -1) 
            {
                strcpy(buffer, "Failed to update loan status.\n");
            } 
            else if (strcmp(new_status, "Approved") == 0) 
            {
                if (credit_to_customer(loan_id, loan_amount) == 0) 
                {
                    strcpy(buffer, "Loan approved and amount credited to customer.\n");
                } 
                else 
                {
                    strcpy(buffer, "Loan approved, but failed to credit amount.\n");
                }
            } else {
                strcpy(buffer, "Loan status updated successfully.\n");
            }
            write(sock, buffer, strlen(buffer));
        }
        else if (strcmp(buffer, "VIEW_CUST_TRANS") == 0)
        {
            printf("Processing VIEW_CUST_TRANS request\n");
            view_customer_transactions(sock);
        }
        else if (strcmp(buffer, "CHANGE_PWD") == 0) 
        {
            char new_pwd[50];
            read(sock, new_pwd, sizeof(new_pwd));
            new_pwd[sizeof(new_pwd) - 1] = '\0';
            size_t len = strlen(new_pwd);
            if (len > 0 && new_pwd[len - 1] == '\n') 
            {
                new_pwd[len - 1] = '\0';
            }
            int status = change_emp_password(id, new_pwd);
            if (status == 0)
                snprintf(buffer, sizeof(buffer), "Password changed\n");
            else
                snprintf(buffer, sizeof(buffer), "Failed to change password try again...\n");
            write(sock, buffer, strlen(buffer));
        }
        else if (strcmp(buffer, "LOGOUT") == 0)
        {
            handle_logout(sock);
            break;
        }
    }
}


/////////////////////////Employee/////////////////////////

/////////////////////////Manager/////////////////////////

int update_customer_status(int custID, const char *action) 
{
    FILE *file = fopen("./data/customer.data", "rb+");
    if (!file) 
    {
        perror("Error opening customer data");
        return 0;
    }

    struct customer cust;
    while (fread(&cust, sizeof(struct customer), 1, file)) 
    {
        if (cust.userID == custID) 
        {
            if (strcmp(action, "ACTIVATE") == 0) 
            {
                strcpy(cust.status, "Active");
            } 
            else 
            {
                strcpy(cust.status, "Inactive");
            }

            fseek(file, -sizeof(struct customer), SEEK_CUR); 
            fwrite(&cust, sizeof(struct customer), 1, file);
            fclose(file);
            return 1; 
        }
    }
    fclose(file);
    return 0;
}

int assign_loan_to_employee(int empID, int loanID) 
{
    FILE *file = fopen("./data/employee.data", "rb+");
    if (!file) 
    {
        perror("Error opening employee data");
        return 0;
    }

    struct employee emp;
    while (fread(&emp, sizeof(struct employee), 1, file)) 
    {
        if (emp.employeeID == empID && emp.loan_count < 10) 
        {
            emp.assigned_loans[emp.loan_count++] = loanID;

            fseek(file, -sizeof(struct employee), SEEK_CUR);
            fwrite(&emp, sizeof(struct employee), 1, file);  
            fclose(file);
            return 1; 
        }
    }

    fclose(file);  
    return 0;  
}

int change_mng_password(int uid, const char *new_password)
{
    int fd = open("./data/manager.data", O_RDWR);
    if (fd == -1)
    {
        perror("Error opening customer file");
        return -1;
    }

    struct flock lock;
    lock.l_type = F_WRLCK;
    lock.l_whence = SEEK_SET;
    lock.l_start = 0;
    lock.l_len = 0; 

    if (fcntl(fd, F_SETLK, &lock) == -1)
    {
        perror("Error locking file");
        close(fd);
        return -1;
    }

    struct manager c;
    int found = 0;
    off_t position;

    while (read(fd, &c, sizeof(struct manager)) == sizeof(struct manager))
    {
        if (c.managerID == uid)
        {
            position = lseek(fd, -sizeof(struct manager), SEEK_CUR);
            if (position == -1)
            {
                perror("lseek failed");
                close(fd);
                return -1;
            }
            strncpy(c.password, new_password, sizeof(c.password) - 1);
            c.password[sizeof(c.password) - 1] = '\0';
            if (write(fd, &c, sizeof(struct manager)) != sizeof(struct manager))
            {
                perror("Failed to write updated record");
                flock(fd, LOCK_UN);
                close(fd);
                return -1;
            }
            found = 1;
            break;
        }
    }

    lock.l_type = F_UNLCK;
    fcntl(fd, F_SETLK, &lock);
    close(fd);
    if (!found)
    {
        printf("User ID %d not found.\n", uid);
        return -1;
    }

    printf("Password changed successfully.\n");
    return 0;
}



void handle_manager_requests(int sock, const char *user_id)
{
    int id = atoi(user_id);
    while (1)
    {
        char buffer[BUFFER_SIZE];
        int bytes_read = read(sock, buffer, BUFFER_SIZE);
        buffer[bytes_read] = '\0';

        if (strcmp(buffer, "ACT/DEACT CUST ACC") == 0)
        {
            int custid;
            char action[20];

            bytes_read = read(sock, buffer, sizeof(buffer) - 1);
            buffer[bytes_read] = '\0';
            sscanf(buffer, "%d", &custid);

            bytes_read = read(sock, action, sizeof(action) - 1);
            action[bytes_read] = '\0';
            action[strcspn(action, "\n")] = '\0';

            if (update_customer_status(custid, action)) 
            {
                sprintf(buffer, "Customer %d %s successfully.\n", custid, action);
            } 
            else 
            {
                sprintf(buffer, "Failed to %s customer %d.\n", action, custid);
            }
            write(sock, buffer, strlen(buffer));
        }
        else if(strcmp(buffer, "ASSIGN LOAN") == 0)
        {
            int loanID, empID;

            bytes_read = read(sock, buffer, sizeof(buffer) - 1);
            buffer[bytes_read] = '\0';
            sscanf(buffer, "%d", &loanID);

            bytes_read = read(sock, buffer, sizeof(buffer) - 1);
            if (bytes_read <= 0) 
            {
                perror("Error reading Employee ID");
                close(sock);
                return;
            }
            buffer[bytes_read] = '\0';
            empID = atoi(buffer);

            if (assign_loan_to_employee(empID, loanID))
            {
                sprintf(buffer, "Loan %d assigned to Employee %d successfully.\n", loanID, empID);
            } 
            else 
            {
                sprintf(buffer, "Failed to assign loan %d to Employee %d.\n", loanID, empID);
            }
            write(sock, buffer, strlen(buffer));
        }
        else if (strcmp(buffer, "VIEW_FEEDBACK") == 0) 
        {
            FILE *file = fopen("./data/feedback.data", "rb");
            if (!file) 
            {
                perror("Error opening feedback file");
                strcpy(buffer, "No feedback available.\n");
                write(sock, buffer, strlen(buffer));
                return;
            }

            struct feedback fb;
            char feedbacks[BUFFER_SIZE * 10] = ""; 
            int count = 0;

            while (fread(&fb, sizeof(struct feedback), 1, file)) 
            {
                char temp[BUFFER_SIZE];
                snprintf(temp, sizeof(temp), "Feedback ID: %d\nCustomer ID: %d\nMessage: %s\nStatus: %s\n\n", fb.feedbackID, fb.customerID, fb.message, fb.status);
                strcat(feedbacks, temp);
                count++;
            }
            fclose(file);

            if (count == 0) 
            {
                strcpy(feedbacks, "No feedback available.\n");
            }

            write(sock, feedbacks, strlen(feedbacks)); 

            int feedback_id;
            int bytes_read = read(sock, buffer, BUFFER_SIZE);
            if (bytes_read <= 0) 
            {
                perror("Error reading feedback ID");
                return;
            }
            buffer[bytes_read] = '\0';
            sscanf(buffer, "%d", &feedback_id);

            FILE *temp_file = fopen("./data/temp_feedback.data", "wb");
            file = fopen("./data/feedback.data", "rb");

            if (!temp_file || !file) 
            {
                perror("Error processing feedback update");
                return;
            }

            int updated = 0;
            while (fread(&fb, sizeof(struct feedback), 1, file)) 
            {
                if (fb.feedbackID == feedback_id) 
                {
                    strcpy(fb.status, "Reviewed"); 
                    updated = 1;
                }
                fwrite(&fb, sizeof(struct feedback), 1, temp_file);
            }
            fclose(file);
            fclose(temp_file);

            remove("./data/feedback.data");
            rename("./data/temp_feedback.data", "./data/feedback.data");

            if (updated) 
            {
                strcpy(buffer, "Feedback status updated to 'Reviewed'.\n");
            } 
            else 
            {
                strcpy(buffer, "Invalid Feedback ID.\n");
            }
            write(sock, buffer, strlen(buffer));
        }
        else if (strcmp(buffer, "CHANGE_PWD") == 0) 
        {
            char new_pwd[50];
            read(sock, new_pwd, sizeof(new_pwd));
            new_pwd[sizeof(new_pwd) - 1] = '\0';
            size_t len = strlen(new_pwd);
            if (len > 0 && new_pwd[len - 1] == '\n') {
                new_pwd[len - 1] = '\0';
            }
            int status = change_mng_password(id, new_pwd);
            if (status == 0)
                snprintf(buffer, sizeof(buffer), "Password changed\n");
            else
                snprintf(buffer, sizeof(buffer), "Failed to change password try again...\n");
            write(sock, buffer, strlen(buffer));
        }
        else if (strcmp(buffer, "LOGOUT") == 0)
        {
            handle_logout(sock);
            break;
        }
    }
}
/////////////////////////Manager/////////////////////////

/////////////////////////Admin/////////////////////////
int add_employee(int uid, const char *fname, const char *lname, const char *pwd) 
{
    int fd = open("./data/employee.data", O_WRONLY | O_APPEND | O_CREAT, 0644);
    if (fd < 0) 
    {
        perror("Failed to open the employee file");
        return -1;
    }

    struct flock lock;
    lock.l_type = F_WRLCK;
    lock.l_whence = SEEK_END;
    lock.l_start = 0;
    lock.l_len = 0;  

    if (fcntl(fd, F_SETLKW, &lock) == -1) 
    {
        perror("Failed to lock the file");
        close(fd);
        return -1;
    }

    struct employee new_employee;
    new_employee.employeeID = uid;
    strncpy(new_employee.first_name, fname, sizeof(new_employee.first_name) - 1);
    new_employee.first_name[sizeof(new_employee.first_name) - 1] = '\0';
    strncpy(new_employee.last_name, lname, sizeof(new_employee.last_name) - 1);
    new_employee.last_name[sizeof(new_employee.last_name) - 1] = '\0';
    strncpy(new_employee.password, pwd, sizeof(new_employee.password) - 1);
    new_employee.password[sizeof(new_employee.password) - 1] = '\0';
    memset(new_employee.assigned_loans, 0, sizeof(new_employee.assigned_loans));
    new_employee.assigned_loans[sizeof(new_employee.assigned_loans) - 1] = '\0';
    new_employee.loan_count = 0;
    strncpy(new_employee.status, "Active", sizeof(new_employee.status) - 1);
    new_employee.status[sizeof(new_employee.status) - 1] = '\0';

    if (write(fd, &new_employee, sizeof(struct employee)) != sizeof(struct employee)) 
    {
        perror("Failed to write new employee to the file");
        fcntl(fd, F_UNLCK, &lock); 
        close(fd);
        return -1;
    }

    lock.l_type = F_UNLCK;
    fcntl(fd, F_SETLK, &lock);
    close(fd);

    printf("New customer (ID: %d) added successfully.\n", uid);
    return 0; 
}

void delete_employee(int employeeID, int sock) {
    char buffer[BUFFER_SIZE];
    int found = 0;

    FILE *fp = fopen("./data/employee.data", "rb");
    FILE *temp = fopen("./data/temp_employee.data", "wb");

    if (!fp || !temp) {
        perror("File error");
        strcpy(buffer, "Error accessing employee data.\n");
        write(sock, buffer, strlen(buffer));
        close(sock);
        return;
    }

    struct employee emp;

    while (fread(&emp, sizeof(struct employee), 1, fp)) 
    {
        if (emp.employeeID == employeeID) 
        {
            found = 1; 
        } 
        else 
        {
            fwrite(&emp, sizeof(struct employee), 1, temp);
        }
    }

    fclose(fp);
    fclose(temp);

    if (found) 
    {
        remove("./data/employee.data");
        rename("./data/temp_employee.data", "./data/employee.data");
        strcpy(buffer, "Employee deleted successfully.\n");
    } 
    else 
    {
        remove("./data/temp_employee.data");
        strcpy(buffer, "Employee not found.\n");
    }

    write(sock, buffer, strlen(buffer));
}

void promote_to_manager(int employeeID, int sock) 
{
    FILE *emp_fp = fopen("./data/employee.data", "rb");
    FILE *mgr_fp = fopen("./data/manager.data", "ab");
    struct employee emp;
    struct manager mgr;
    char buffer[BUFFER_SIZE];
    int found = 0;

    if (!emp_fp || !mgr_fp) 
    {
        perror("Error opening files");
        strcpy(buffer, "Error accessing data.\n");
        write(sock, buffer, strlen(buffer));
        return;
    }

    FILE *temp_fp = fopen("./data/temp_employee.data", "wb");
    while (fread(&emp, sizeof(struct employee), 1, emp_fp)) 
    {
        if (emp.employeeID == employeeID) 
        {
            found = 1;
            mgr.managerID = emp.employeeID;
            strncpy(mgr.first_name, emp.first_name, sizeof(mgr.first_name) - 1);
            mgr.first_name[sizeof(mgr.first_name) - 1] = '\0';
            strncpy(mgr.last_name, emp.last_name, sizeof(mgr.last_name) - 1);
            mgr.last_name[sizeof(mgr.last_name) - 1] = '\0';
            strncpy(mgr.password, emp.password, sizeof(mgr.password) - 1);
            mgr.password[sizeof(mgr.password) - 1] = '\0';

            fwrite(&mgr, sizeof(struct manager), 1, mgr_fp);
        } 
        else 
        {
            fwrite(&emp, sizeof(struct employee), 1, temp_fp); 
        }
    }

    fclose(emp_fp);
    fclose(mgr_fp);
    fclose(temp_fp);

    remove("./data/employee.data");
    rename("./data/temp_employee.data", "./data/employee.data");

    if (found) 
    {
        strcpy(buffer, "Employee promoted to Manager successfully.\n");
    } 
    else 
    {
        strcpy(buffer, "Employee not found.\n");
    }
    write(sock, buffer, strlen(buffer));
}

void demote_to_employee(int managerID, int sock) 
{
    FILE *mgr_fp = fopen("./data/manager.data", "rb");
    FILE *emp_fp = fopen("./data/employee.data", "ab");
    struct manager mgr;
    struct employee emp;
    char buffer[BUFFER_SIZE];
    int found = 0;

    if (!mgr_fp || !emp_fp) 
    {
        perror("Error opening files");
        strcpy(buffer, "Error accessing data.\n");
        write(sock, buffer, strlen(buffer));
        return;
    }

    FILE *temp_fp = fopen("./data/temp_manager.data", "wb");
    while (fread(&mgr, sizeof(struct manager), 1, mgr_fp)) 
    {
        if (mgr.managerID == managerID) 
        {
            found = 1;
            emp.employeeID = mgr.managerID;
            strncpy(emp.first_name, mgr.first_name, sizeof(emp.first_name) - 1);
            emp.first_name[sizeof(emp.first_name) - 1] = '\0';
            strncpy(emp.last_name, mgr.last_name, sizeof(emp.last_name) - 1);
            emp.last_name[sizeof(emp.last_name) - 1] = '\0';
            strncpy(emp.password, mgr.password, sizeof(emp.password) - 1);
            emp.password[sizeof(emp.password) - 1] = '\0';
            strcpy(emp.status, "Active");
            emp.loan_count = 0; 
            memset(emp.assigned_loans, 0, sizeof(emp.assigned_loans));
            emp.assigned_loans[sizeof(emp.assigned_loans) - 1] = '\0';

            fwrite(&emp, sizeof(struct employee), 1, emp_fp);
        } 
        else 
        {
            fwrite(&mgr, sizeof(struct manager), 1, temp_fp); 
        }
    }

    fclose(mgr_fp);
    fclose(emp_fp);
    fclose(temp_fp);

    remove("./data/manager.data");
    rename("./data/temp_manager.data", "./data/manager.data");

    if (found) 
    {
        strcpy(buffer, "Manager demoted to Employee successfully.\n");
    } 
    else 
    {
        strcpy(buffer, "Manager not found.\n");
    }
    write(sock, buffer, strlen(buffer));
}

void manage_user_roles(int sock) 
{
    char buffer[BUFFER_SIZE];
    int id, found = 0;
    char new_role[20];

    int bytes_read = read(sock, buffer, sizeof(buffer) - 1);
    if (bytes_read <= 0) 
    {
        perror("Error reading ID");
        close(sock);
        return;
    }
    buffer[bytes_read] = '\0';
    sscanf(buffer, "%d", &id); 

    bytes_read = read(sock, new_role, sizeof(new_role) - 1);
    if (bytes_read <= 0) 
    {
        perror("Error reading new role");
        close(sock);
        return;
    }
    new_role[bytes_read] = '\0';

    if (strcmp(new_role, "Manager") == 0) 
    {
        promote_to_manager(id, sock);
    } 
    else if (strcmp(new_role, "Employee") == 0) 
    {
        demote_to_employee(id, sock);
    } 
    else 
    {
        strcpy(buffer, "Invalid role specified.\n");
        write(sock, buffer, strlen(buffer));
    }
}


int change_admin_password(int uid, const char *new_password)
{
    int fd = open("./data/admin.data", O_RDWR);
    if (fd == -1)
    {
        perror("Error opening customer file");
        return -1;
    }

    struct flock lock;
    lock.l_type = F_WRLCK; 
    lock.l_whence = SEEK_SET;
    lock.l_start = 0;
    lock.l_len = 0; 

    if (fcntl(fd, F_SETLK, &lock) == -1)
    {
        perror("Error locking file");
        close(fd);
        return -1;
    }

    struct manager c;
    int found = 0;
    off_t position;

    while (read(fd, &c, sizeof(struct manager)) == sizeof(struct manager))
    {
        if (c.managerID == uid)
        {
            position = lseek(fd, -sizeof(struct manager), SEEK_CUR);
            if (position == -1)
            {
                perror("lseek failed");
                close(fd);
                return -1;
            }
            strncpy(c.password, new_password, sizeof(c.password) - 1);
            c.password[sizeof(c.password) - 1] = '\0';
            if (write(fd, &c, sizeof(struct manager)) != sizeof(struct manager))
            {
                perror("Failed to write updated record");
                flock(fd, LOCK_UN); 
                close(fd);
                return -1;
            }
            found = 1;
            break;
        }
    }

    lock.l_type = F_UNLCK; 
    fcntl(fd, F_SETLK, &lock);
    close(fd);
    if (!found)
    {
        printf("User ID %d not found.\n", uid);
        return -1;
    }

    printf("Password changed successfully.\n");
    return 0;
}


void handle_admin_requests(int sock, const char *user_id)
{
    int id = atoi(user_id);
    while (1)
    {
        char buffer[BUFFER_SIZE];
        int bytes_read = read(sock, buffer, BUFFER_SIZE);
        buffer[bytes_read] = '\0';

        if (strcmp(buffer, "ADD_EMPLOYEE") == 0)
        {
            int eid;
            char fname[30], lname[30], pwd[50];

            if (read(sock, buffer, sizeof(buffer) - 1) <= 0) 
            {
                perror("Error reading User ID from client");
                close(sock);
                return;
            }
            sscanf(buffer, "%d", &eid);

            bytes_read = read(sock, fname, sizeof(fname) - 1);
            fname[bytes_read] = '\0';
            fname[strcspn(fname, "\n")] = '\0';


            bytes_read = read(sock, lname, sizeof(lname) - 1);
            lname[bytes_read] = '\0';
            lname[strcspn(lname, "\n")] = '\0';

            bytes_read = read(sock, pwd, sizeof(pwd) - 1);
            pwd[bytes_read] = '\0';
            pwd[strcspn(pwd, "\n")] = '\0';

            if (add_employee(eid, fname, lname, pwd) == 0) {
                strcpy(buffer, "Customer added successfully.\n");
            } else {
                strcpy(buffer, "Failed to add customer.\n");
            }
            write(sock, buffer, strlen(buffer));
        }
        else if (strcmp(buffer, "MANAGE_CUSTOMER") == 0) 
        {
            int uid;
            char new_fname[50], new_lname[50],new_status[20], new_password[50], buffer[BUFFER_SIZE];
            float new_balance, new_loan;
            int option, bytes_read;

            bytes_read = read(sock, buffer, sizeof(buffer) - 1);
            buffer[bytes_read] = '\0';
            sscanf(buffer, "%d", &uid);

            struct customer c;
            int fd = open("./data/customer.data", O_RDWR);
            if (fd == -1) {
                perror("Error opening customer file");
                strcpy(buffer, "Error: Unable to open customer file.\n");
                write(sock, buffer, strlen(buffer));
                return;
            }

            struct flock lock;
            lock.l_type = F_WRLCK;
            lock.l_whence = SEEK_SET;
            lock.l_start = 0;
            lock.l_len = 0; 

            if (fcntl(fd, F_SETLK, &lock) == -1) 
            {
                perror("Error locking customer file");
                strcpy(buffer, "Error: Unable to lock the file.\n");
                write(sock, buffer, strlen(buffer));
                close(fd);
                return;
            }

            int found = 0;
            off_t position;

            while (read(fd, &c, sizeof(struct customer)) == sizeof(struct customer)) 
            {
                if (c.userID == uid) 
                {
                    found = 1;
                    position = lseek(fd, -sizeof(struct customer), SEEK_CUR);
                    break;
                }
            }

            if (!found) 
            {
                snprintf(buffer, sizeof(buffer), "Customer with ID %d not found.\n", uid);
                write(sock, buffer, strlen(buffer));
                flock(fd, F_UNLCK);
                close(fd);
                return;
            }

            snprintf(buffer, sizeof(buffer), "ID: %d\nName: %s %s\nBalance: %.2f\nLoan: %.2f\nStatus: %s\n", c.userID, c.firstName, c.lastName, c.balance, c.loan, c.status);
            write(sock, buffer, strlen(buffer));

            bytes_read = read(sock, buffer, sizeof(buffer) - 1);
            buffer[bytes_read] = '\0';
            option = atoi(buffer);

            switch (option) 
            {
                case 1: 

                    read(sock, new_fname, sizeof(new_fname));
                    new_fname[sizeof(new_fname) - 1] = '\0';
                    size_t len = strlen(new_fname);
                    if (len > 0 && new_fname[len - 1] == '\n') 
                    {
                        new_fname[len - 1] = '\0';
                    }

                    read(sock, new_lname, sizeof(new_lname));
                    new_lname[sizeof(new_lname) - 1] = '\0';
                    len = strlen(new_lname);
                    if (len > 0 && new_lname[len - 1] == '\n') 
                    {
                        new_lname[len - 1] = '\0';
                    }

                    bytes_read = read(sock, buffer, sizeof(buffer) - 1);
                    buffer[bytes_read] = '\0';
                    sscanf(buffer, "%f", &new_balance);

                    bytes_read = read(sock, buffer, sizeof(buffer) - 1);
                    buffer[bytes_read] = '\0';
                    sscanf(buffer, "%f", &new_loan);

                    read(sock, new_status, sizeof(new_status));
                    new_status[sizeof(new_status) - 1] = '\0';
                    len = strlen(new_status);
                    if (len > 0 && new_status[len - 1] == '\n') 
                    {
                        new_status[len - 1] = '\0';
                    }

                    strncpy(c.firstName, new_fname, sizeof(c.firstName) - 1);
                    strncpy(c.lastName, new_lname, sizeof(c.lastName) - 1);
                    c.balance = new_balance;
                    c.loan = new_loan;
                    strncpy(c.status, new_status, sizeof(c.status) - 1);

                    lseek(fd, position, SEEK_SET);
                    if (write(fd, &c, sizeof(struct customer)) != sizeof(struct customer)) 
                    {
                        perror("Failed to write updated customer record");
                        strcpy(buffer, "Error: Failed to update customer.\n");
                        write(sock, buffer, strlen(buffer));
                    } 
                    else 
                    {
                        strcpy(buffer, "Customer details updated successfully.\n");
                        write(sock, buffer, strlen(buffer));
                    }
                    break;

                case 2:
                    delete_customer(uid, sock);
                    break;

                case 3:
                    bytes_read = read(sock, new_password, sizeof(new_password) - 1);
                    new_password[bytes_read] = '\0';
                    len = strlen(new_password);
                    if (len > 0 && new_password[len - 1] == '\n') 
                    {
                        new_password[len - 1] = '\0';
                    }

                    strncpy(c.password, new_password, sizeof(c.password) - 1);

                    lseek(fd, position, SEEK_SET);
                    if (write(fd, &c, sizeof(struct customer)) != sizeof(struct customer)) 
                    {
                        perror("Failed to reset password");
                        strcpy(buffer, "Error: Failed to reset password.\n");
                        write(sock, buffer, strlen(buffer));
                    } 
                    else 
                    {
                        strcpy(buffer, "Password reset successfully.\n");
                        write(sock, buffer, strlen(buffer));
                    }
                    break;

                case 4:
                    strcpy(buffer, "Exiting customer management.\n");
                    write(sock, buffer, strlen(buffer));
                    break;

                default:
                    strcpy(buffer, "Invalid option.\n");
                    write(sock, buffer, strlen(buffer));
                    break;
            }
            flock(fd, F_UNLCK);
            close(fd);
        }
        else if (strcmp(buffer, "MANAGE_EMPLOYEE") == 0) 
        {
            int uid;
            char new_fname[50], new_lname[50], new_status[20], new_password[50], buffer[BUFFER_SIZE];
            int option, bytes_read;

            bytes_read = read(sock, buffer, sizeof(buffer) - 1);
            buffer[bytes_read] = '\0';
            sscanf(buffer, "%d", &uid);

            struct employee c;
            int fd = open("./data/employee.data", O_RDWR);
            if (fd == -1) 
            {
                perror("Error opening employee file");
                strcpy(buffer, "Error: Unable to open employee file.\n");
                write(sock, buffer, strlen(buffer));
                return;
            }

            struct flock lock;
            lock.l_type = F_WRLCK;
            lock.l_whence = SEEK_SET;
            lock.l_start = 0;
            lock.l_len = 0;

            if (fcntl(fd, F_SETLK, &lock) == -1) 
            {
                perror("Error locking employee file");
                strcpy(buffer, "Error: Unable to lock the file.\n");
                write(sock, buffer, strlen(buffer));
                close(fd);
                return;
            }

            int found = 0;
            off_t position;

            while (read(fd, &c, sizeof(struct employee)) == sizeof(struct employee)) 
            {
                if (c.employeeID == uid) 
                {
                    found = 1;
                    position = lseek(fd, -sizeof(struct employee), SEEK_CUR);
                    break;
                }
            }

            if (!found) 
            {
                snprintf(buffer, sizeof(buffer), "Employee with ID %d not found.\n", uid);
                write(sock, buffer, strlen(buffer));
                flock(fd, F_UNLCK);
                close(fd);
                return;
            }

            snprintf(buffer, sizeof(buffer), "EMP ID: %d\nName: %s %s\nStatus: %s\n", c.employeeID, c.first_name, c.last_name, c.status);
            write(sock, buffer, strlen(buffer));

            bytes_read = read(sock, buffer, sizeof(buffer) - 1);
            buffer[bytes_read] = '\0';
            option = atoi(buffer);

            switch (option) 
            {
                case 1:
                    read(sock, new_fname, sizeof(new_fname));
                    new_fname[sizeof(new_fname) - 1] = '\0';
                    size_t len = strlen(new_fname);
                    if (len > 0 && new_fname[len - 1] == '\n') 
                    {
                        new_fname[len - 1] = '\0';
                    }

                    read(sock, new_lname, sizeof(new_lname));
                    new_lname[sizeof(new_lname) - 1] = '\0';
                    len = strlen(new_lname);
                    if (len > 0 && new_lname[len - 1] == '\n') 
                    {
                        new_lname[len - 1] = '\0';
                    }

                    read(sock, new_status, sizeof(new_status));
                    new_status[sizeof(new_status) - 1] = '\0';
                    len = strlen(new_status);
                    if (len > 0 && new_status[len - 1] == '\n') 
                    {
                        new_status[len - 1] = '\0';
                    }

                    strncpy(c.first_name, new_fname, sizeof(c.first_name) - 1);
                    strncpy(c.last_name, new_lname, sizeof(c.last_name) - 1);
                    strncpy(c.status, new_status, sizeof(c.status) - 1);

                    lseek(fd, position, SEEK_SET);
                    if (write(fd, &c, sizeof(struct employee)) != sizeof(struct employee)) 
                    {
                        perror("Failed to write updated employee record");
                        strcpy(buffer, "Error: Failed to update employee.\n");
                        write(sock, buffer, strlen(buffer));
                    } 
                    else 
                    {
                        strcpy(buffer, "Employee details updated successfully.\n");
                        write(sock, buffer, strlen(buffer));
                    }
                    break;

                case 2:
                    delete_employee(uid, sock);
                    break;

                case 3:
                    bytes_read = read(sock, new_password, sizeof(new_password) - 1);
                    new_password[bytes_read] = '\0';
                    new_password[strcspn(new_password, "\n")] = '\0'; 
                    new_password[sizeof(new_password) - 1] = '\0';
                    len = strlen(new_password);
                    if (len > 0 && new_password[len - 1] == '\n') 
                    {
                        new_password[len - 1] = '\0';
                    }

                    strncpy(c.password, new_password, sizeof(c.password) - 1);

                    lseek(fd, position, SEEK_SET);
                    if (write(fd, &c, sizeof(struct employee)) != sizeof(struct employee)) 
                    {
                        perror("Failed to reset password");
                        strcpy(buffer, "Error: Failed to reset password.\n");
                        write(sock, buffer, strlen(buffer));
                    } 
                    else 
                    {
                        strcpy(buffer, "Password reset successfully.\n");
                        write(sock, buffer, strlen(buffer));
                    }
                    break;

                case 4:
                    strcpy(buffer, "Exiting employee management.\n");
                    write(sock, buffer, strlen(buffer));
                    break;

                default:
                    strcpy(buffer, "Invalid option.\n");
                    write(sock, buffer, strlen(buffer));
                    break;
            }
            flock(fd, F_UNLCK);
            close(fd);
        }
        else if (strcmp(buffer, "MANAGE_USER_ROLES") == 0) 
        {
            manage_user_roles(sock);
        }
        else if (strcmp(buffer, "CHANGE_PWD") == 0) 
        {
            char new_pwd[50];
            read(sock, new_pwd, sizeof(new_pwd));
            new_pwd[sizeof(new_pwd) - 1] = '\0';
            size_t len = strlen(new_pwd);
            if (len > 0 && new_pwd[len - 1] == '\n') 
            {
                new_pwd[len - 1] = '\0';
            }
            int status = change_admin_password(id, new_pwd);
            if (status == 0)
                snprintf(buffer, sizeof(buffer), "Password changed\n");
            else
                snprintf(buffer, sizeof(buffer), "Failed to change password try again...\n");
            write(sock, buffer, strlen(buffer));
        }
        else if (strcmp(buffer, "LOGOUT") == 0)
        {
            handle_logout(sock);
            break;
        }
    }
}
/////////////////////////Admin/////////////////////////




int validate_login(const char *role, const char *uid, const char *password) {
	int id = atoi(uid);
    FILE *file;
    char filename[50];
    
    if (strcmp(role, "customer") == 0) {
        strcpy(filename, "./data/customer.data");
    } else if (strcmp(role, "employee") == 0) {
        strcpy(filename, "./data/employee.data");
    } else if (strcmp(role, "manager") == 0) {
        strcpy(filename, "./data/manager.data");
    } else if (strcmp(role, "admin") == 0) {
        strcpy(filename, "./data/admin.data");
    } else {
        return 0; 
    }

    file = fopen(filename, "rb");
    if (file == NULL) 
    {
        perror("File open error");
        return 0;
    }

    if (strcmp(role, "customer") == 0) 
    {
        struct customer c;
        while (fread(&c, sizeof(struct customer), 1, file) == 1) 
        {
            if ((c.userID == id) && strcmp(c.password, password) == 0) 
            {
                fclose(file);
                return 1;
            }
        }
    }
    else if (strcmp(role, "employee") == 0) 
    {
        struct employee emp;
        while (fread(&emp, sizeof(struct employee), 1, file) == 1) 
        {
            if ((emp.employeeID == id) && strcmp(emp.password, password) == 0) 
            {
                fclose(file);
                return 1;
            }
        }
    } 
    else if (strcmp(role, "manager") == 0) 
    {
        struct manager mgr;
        while (fread(&mgr, sizeof(struct manager), 1, file) == 1) 
        {
            if ((mgr.managerID == id) && strcmp(mgr.password, password) == 0) 
            {
                fclose(file);
                return 1; 
            }
        }
    } 
    else if (strcmp(role, "admin") == 0) 
    {
        struct admin admin;
        while (fread(&admin, sizeof(struct admin), 1, file) == 1) 
        {
            if ((admin.adminID == id) && strcmp(admin.password, password) == 0) 
            {
                fclose(file);
                return 1; 
            }
        }
    }

    fclose(file);
    return 0;
}


void *handle_client(void *client_socket)
{
    int sock = *(int *)client_socket;
    char buffer[BUFFER_SIZE], role[20], id[10], password[50];
    int bytes_read;

    bytes_read = read(sock, role, sizeof(role) - 1);
    role[bytes_read] = '\0';

    bytes_read = read(sock, buffer, sizeof(buffer) - 1);
    buffer[bytes_read] = '\0';

    sscanf(buffer, "%s %s", id, password);

    if (validate_login(role, id, password))
    {
        write(sock, "SUCCESS", strlen("SUCCESS"));

        if (strcmp(role, "customer") == 0)
        {
            handle_customer_requests(sock, id); 
        }
        else if (strcmp(role, "employee") == 0)
        {
            handle_employee_requests(sock, id);
        }
        else if (strcmp(role, "manager") == 0)
        {
            handle_manager_requests(sock, id); 
        }
        else if (strcmp(role, "admin") == 0)
        {
            handle_admin_requests(sock, id);
        }
    }
    else
    {
        write(sock, "Invalid ID or Password or Role", strlen("Invalid ID or Password or Role"));
    }

    close(sock);
    free(client_socket);
    pthread_exit(NULL);
}


int main()
{
    int server_fd, new_socket;
    struct sockaddr_in address;
    int addrlen = sizeof(address);
    pthread_t thread_id;

    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0)
    {
        perror("Socket failed");
        exit(EXIT_FAILURE);
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0)
    {
        perror("Bind failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    if (listen(server_fd, 10) < 0)
    {
        perror("Listen failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    printf("Server is listening on port %d...\n", PORT);

    while (1)
    {
        new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t *)&addrlen);
        if (new_socket < 0)
        {
            perror("Accept failed");
            continue;
        }

        printf("New client connected!\n");

        int *client_sock = (int *)malloc(sizeof(int));
        *client_sock = new_socket;

        pthread_create(&thread_id, NULL, handle_client, (void *)client_sock);
    }

    close(server_fd);
    return 0;
}